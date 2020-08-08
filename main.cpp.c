#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "timer.h"
#include "http_conn.h"
#include "log.h"
#include "sql_connection_pool.h"


#define MAX_FD 65536            //最大文件描述符
#define MAX_EVENT_NUMBER 10000	//最大事件数
#define TIMESLOT 5				//最小超时单位

#define SYNSQL //同步数据库校验
//#define CGISQLPOOL    //CGI数据库校验
#define SYNLOG //同步写日志
//#define ASYNLOG       //异步写日志

//#define ET            //边缘触发非阻塞
#define LT //水平触发阻塞

extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblock(int fd);

//设置定时器相关参数
static int pipefd[2];
//创建定时器链表容器
static sort_timer_lst timer_lst;
static int epollfd = 0;



//信号处理函数
	void sig_handler(int sig) {
	//为了保证函数的可重入性，保留原理的errno
	//可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
		int save_errno = errno;
		int msg = sig;

		//将信号值从管道的写端写入
		send(pipefd[1], (char*)&msg, 1, 0);
		//将原来的errno赋值为当前errno
		errno = save_errno;
	}

	void addsig(int sig, void(handler)(int), bool restart = true) {
		struct sigaction sa;
		memset(&sa, '\0', sizeof(sa));
		sa.sa_handler = handler;
		//这里restart的意思是收到信号后，继续运行该函数检测信号
		if (restart)
			sa.sa_flags |= SA_RESTART;
		//将所有信号添加到信号集中
		sigfillset(&sa.sa_mask);
		//执行sigaction函数
		assert(sigaction(sig, &sa, NULL) != -1);
	}

	//定时器回调函数
	void cb_func(client_data* user_data) {
	//删除非活动连接在socket上的注册事件
		epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
		assert(user_data);

		//关闭文件描述符
		close(user_data->sockfd);
		//减少连接数
		http_conn::m_user_count--;
		LOG_INFO("close fd %d", user_data->sockfd);
    	Log::get_instance()->flush();
	}

	void show_error(int connfd, const char *info)
	{
		printf("%s", info);
		send(connfd, info, strlen(info), 0);
		close(connfd);
	}

	//SIGALRM信号的处理：不断地重新定时
	void timer_handler() {
		timer_lst.tick();
		alarm(TIMESLOT);
	}


int main(int argc, char *argv[]){
	#ifdef ASYNLOG
    	Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型
	#endif

	#ifdef SYNLOG
    	Log::get_instance()->init("ServerLog", 2000, 800000, 0); //同步日志模型
	#endif

    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

	int port = atoi(argv[1]);

	addsig(SIGPIPE,SIG_IGN);

	//创建数据库连接池
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "root", "yourdb", 3306, 8);

	//创建线程池,需要用数据库连接池来new线程池
    ThreadPool<http_conn> *pool = NULL;
    try
    {
        pool = new ThreadPool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }
    //创建MAX_FD个http类对象
    http_conn *users = new http_conn[MAX_FD];
    assert(users);

	#ifdef SYNSQL
    	//初始化数据库读取表
    	users->initmysql_result(connPool);
	#endif

	#ifdef CGISQLPOOL
   		//初始化数据库读取表
    	users->initresultFile(connPool);
	#endif

	int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

	int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

	int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

	//创建内核事件表
	epoll_event events[MAX_EVENT_NUMBER];
	epollfd = epoll_create(5);
	assert(epollfd != -1);

	//将监听套接字放在epoll树上
	addfd(epollfd, listenfd, false);

	//将epollfd赋值给http类对象的m_epollfd属性
	http_conn::m_epollfd = epollfd;

	//创建管道套接字
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret != -1);
	//设置管道写端非阻塞
	setnonblock(pipefd[1]);
	//设置管道读端未ET非阻塞
	addfd(epollfd, pipefd[0], false);

	//传递给主循环的信号值，只关注SIGALRM和SIGTERM
	addsig(SIGALRM, sig_handler, false);
	addsig(SIGTERM, sig_handler, false);
	//循环条件
	bool stop_server = false;

	//创建连接资源数组
	client_data *users_timer = new client_data[MAX_FD];
	//超时标志初始化为false
	bool timeout = false;
	//alarm定时触发SIGALRM信号
	alarm(TIMESLOT);

	while (!stop_server){
	int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
	if (number < 0 && errno != EINTR) {
		LOG_ERROR("%s", "epoll failure");
		break;
	}
	//对就绪事件进行处理
	for (int i = 0; i < number; ++i) {
		int sockfd = events[i].data.fd;
		if (sockfd == listenfd) {
			struct sockaddr_in client_address;
			socklen_t client_addrlength = sizeof(client_address);
#ifdef LT
			int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
			if (connfd < 0)
			{
				LOG_ERROR("%s:errno is:%d", "accept error", errno);
				continue;
			}
			if (http_conn::m_user_count >= MAX_FD)
			{
				show_error(connfd, "Internal server busy");
				LOG_ERROR("%s", "Internal server busy");
				continue;
			}
			users[connfd].init(connfd, client_address);

			//初始化client_data数据
			//创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
			users_timer[connfd].address = client_address;
			users_timer[connfd].sockfd = connfd;
			util_timer *timer = new util_timer;
			timer->user_data = &users_timer[connfd];
			timer->cb_func = cb_func;
			time_t cur = time(NULL);
			timer->expire = cur + 3 * TIMESLOT;
			users_timer[connfd].timer = timer;
			timer_lst.add_timer(timer);
#endif

			//ET非阻塞边沿触发，需要循环接收数据，因为边沿触发只会提醒一次，那么就把这次收到的所有数据都一次写入
#ifdef ET
			while (1)
			{
				int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
				if (connfd < 0)
				{
					LOG_ERROR("%s:errno is:%d", "accept error", errno);
					break;
				}
				if (http_conn::m_user_count >= MAX_FD)
				{
					show_error(connfd, "Internal server busy");
					LOG_ERROR("%s", "Internal server busy");
					break;
				}
				users[connfd].init(connfd, client_address);

				//初始化client_data数据
				//创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
				users_timer[connfd].address = client_address;
				users_timer[connfd].sockfd = connfd;
				util_timer *timer = new util_timer;
				timer->user_data = &users_timer[connfd];
				timer->cb_func = cb_func;
				time_t cur = time(NULL);
				timer->expire = cur + 3 * TIMESLOT;
				users_timer[connfd].timer = timer;
				timer_lst.add_timer(timer);
			}
			continue;
#endif
		}
		//处理异常事件
		else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
			//服务器端关闭连接,移除对应定时器
			util_timer *timer = users_timer[sockfd].timer;
			timer->cb_func(&users_timer[sockfd]);
			if(timer){
				timer_lst.del_timer(timer);
			}
		}

		//处理信号
		//管道读端对应的文件描述符发生读事件,读管道加入了epoll树，本质上也是fd
		else if ((sockfd == pipefd[0]) && (events[i].events&EPOLLIN)) {
			int sig;
            char signals[1024];
			//从管道读端读出信号值，成功返回字节数，失败-1
			//正常情况下ret返回值总是1，只有14和15两个ASCII码对应的字符
			//SIGALRM 14
			//SIGTERM 15
			ret = recv(pipefd[0], signals, sizeof(signals), 0);
			if (ret == -1)
			{
				continue;
			}
			else if (ret == 0)
			{
				continue;
			}
			else
			{
				for (int i = 0; i < ret; ++i)
				{
					switch (signals[i])
					{
					case SIGALRM:
					{
						timeout = true;
						break;
					}
					case SIGTERM:
					{
						stop_server = true;
					}
					}
				}
			}				
		}

		//处理客户端上接收到的数据
		else if (events[i].events&EPOLLIN) {
			//读入对应缓冲区,这里sockfd是文件描述符树里的文件描述符，users是创建的65535个http_conn对象
			//之所以创建大量的对象，就是让所有的sockfd有对象可以接管，文件描述符是0-65535，而文件描述符则对应到users的数组序号
			util_timer *timer = users_timer[sockfd].timer;
			if (users[sockfd].read_once()) {
				LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                Log::get_instance()->flush();
				//检测到读事件，放入请求队列
				pool->append(users + sockfd);
				//若有数据传输，则将定时器往后延迟3个单位
				//并对新的定时器在链表上的位置进行调整
				if (timer)
				{
					time_t cur = time(NULL);
					timer->expire = cur + 3 * TIMESLOT;
					LOG_INFO("%s", "adjust timer once");
					Log::get_instance()->flush();
					timer_lst.adjust_timer(timer);
				}
			}
			else{
					//服务器关闭连接
					timer->cb_func(&users_timer[sockfd]);
					if(timer){
						timer_lst.del_timer(timer);
					}
				}
			}

		//处理客户连接上接收到的数据
		else if (events[i].events & EPOLLOUT)
		{
			util_timer *timer = users_timer[sockfd].timer;
			if (users[sockfd].write())
			{
				LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
				Log::get_instance()->flush();

				//若有数据传输，则将定时器往后延迟3个单位
				//并对新的定时器在链表上的位置进行调整
				if (timer)
				{
					time_t cur = time(NULL);
					timer->expire = cur + 3 * TIMESLOT;
					LOG_INFO("%s", "adjust timer once");
					Log::get_instance()->flush();
					timer_lst.adjust_timer(timer);
				}
			}
		else {
		//收不到数据说明对方断开了连接，关闭服务器连接，移除对应的定时器
			timer->cb_func(&users_timer[sockfd]);
			if (timer)
				timer_lst.del_timer(timer);
				}
			}
		}
		//处理定时器为非必须事件，收到信号并不是马上处理
		//完成读写事件后，再处理SIGALRM信号，反复执行，每三秒执行一次
		if (timeout) {
			timer_handler();
			timeout = false;
		}
	}
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;

}
