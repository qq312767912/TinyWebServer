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


#define MAX_FD 65536            //����ļ�������
#define MAX_EVENT_NUMBER 10000	//����¼���
#define TIMESLOT 5				//��С��ʱ��λ

#define SYNSQL //ͬ�����ݿ�У��
//#define CGISQLPOOL    //CGI���ݿ�У��
#define SYNLOG //ͬ��д��־
//#define ASYNLOG       //�첽д��־

//#define ET            //��Ե����������
#define LT //ˮƽ��������

extern int addfd(int epollfd, int fd, bool one_shot);
extern int remove(int epollfd, int fd);
extern int setnonblock(int fd);

//���ö�ʱ����ز���
static int pipefd[2];
//������ʱ����������
static sort_timer_lst timer_lst;
static int epollfd = 0;



//�źŴ�����
	void sig_handler(int sig) {
	//Ϊ�˱�֤�����Ŀ������ԣ�����ԭ���errno
	//�������Ա�ʾ�жϺ��ٴν���ú���������������֮ǰ��ͬ�����ᶪʧ����
		int save_errno = errno;
		int msg = sig;

		//���ź�ֵ�ӹܵ���д��д��
		send(pipefd[1], (char*)&msg, 1, 0);
		//��ԭ����errno��ֵΪ��ǰerrno
		errno = save_errno;
	}

	void addsig(int sig, void(handler)(int), bool restart = true) {
		struct sigaction sa;
		memset(&sa, '\0', sizeof(sa));
		sa.sa_handler = handler;
		//����restart����˼���յ��źź󣬼������иú�������ź�
		if (restart)
			sa.sa_flags |= SA_RESTART;
		//�������ź���ӵ��źż���
		sigfillset(&sa.sa_mask);
		//ִ��sigaction����
		assert(sigaction(sig, &sa, NULL) != -1);
	}

	//��ʱ���ص�����
	void cb_func(client_data* user_data) {
	//ɾ���ǻ������socket�ϵ�ע���¼�
		epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
		assert(user_data);

		//�ر��ļ�������
		close(user_data->sockfd);
		//����������
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

	//SIGALRM�źŵĴ������ϵ����¶�ʱ
	void timer_handler() {
		timer_lst.tick();
		alarm(TIMESLOT);
	}


int main(int argc, char *argv[]){
	#ifdef ASYNLOG
    	Log::get_instance()->init("ServerLog", 2000, 800000, 8); //�첽��־ģ��
	#endif

	#ifdef SYNLOG
    	Log::get_instance()->init("ServerLog", 2000, 800000, 0); //ͬ����־ģ��
	#endif

    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

	int port = atoi(argv[1]);

	addsig(SIGPIPE,SIG_IGN);

	//�������ݿ����ӳ�
    connection_pool *connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "root", "yourdb", 3306, 8);

	//�����̳߳�,��Ҫ�����ݿ����ӳ���new�̳߳�
    ThreadPool<http_conn> *pool = NULL;
    try
    {
        pool = new ThreadPool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }
    //����MAX_FD��http�����
    http_conn *users = new http_conn[MAX_FD];
    assert(users);

	#ifdef SYNSQL
    	//��ʼ�����ݿ��ȡ��
    	users->initmysql_result(connPool);
	#endif

	#ifdef CGISQLPOOL
   		//��ʼ�����ݿ��ȡ��
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

	//�����ں��¼���
	epoll_event events[MAX_EVENT_NUMBER];
	epollfd = epoll_create(5);
	assert(epollfd != -1);

	//�������׽��ַ���epoll����
	addfd(epollfd, listenfd, false);

	//��epollfd��ֵ��http������m_epollfd����
	http_conn::m_epollfd = epollfd;

	//�����ܵ��׽���
	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
	assert(ret != -1);
	//���ùܵ�д�˷�����
	setnonblock(pipefd[1]);
	//���ùܵ�����δET������
	addfd(epollfd, pipefd[0], false);

	//���ݸ���ѭ�����ź�ֵ��ֻ��עSIGALRM��SIGTERM
	addsig(SIGALRM, sig_handler, false);
	addsig(SIGTERM, sig_handler, false);
	//ѭ������
	bool stop_server = false;

	//����������Դ����
	client_data *users_timer = new client_data[MAX_FD];
	//��ʱ��־��ʼ��Ϊfalse
	bool timeout = false;
	//alarm��ʱ����SIGALRM�ź�
	alarm(TIMESLOT);

	while (!stop_server){
	int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
	if (number < 0 && errno != EINTR) {
		LOG_ERROR("%s", "epoll failure");
		break;
	}
	//�Ծ����¼����д���
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

			//��ʼ��client_data����
			//������ʱ�������ûص������ͳ�ʱʱ�䣬���û����ݣ�����ʱ����ӵ�������
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

			//ET���������ش�������Ҫѭ���������ݣ���Ϊ���ش���ֻ������һ�Σ���ô�Ͱ�����յ����������ݶ�һ��д��
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

				//��ʼ��client_data����
				//������ʱ�������ûص������ͳ�ʱʱ�䣬���û����ݣ�����ʱ����ӵ�������
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
		//�����쳣�¼�
		else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
			//�������˹ر�����,�Ƴ���Ӧ��ʱ��
			util_timer *timer = users_timer[sockfd].timer;
			timer->cb_func(&users_timer[sockfd]);
			if(timer){
				timer_lst.del_timer(timer);
			}
		}

		//�����ź�
		//�ܵ����˶�Ӧ���ļ��������������¼�,���ܵ�������epoll����������Ҳ��fd
		else if ((sockfd == pipefd[0]) && (events[i].events&EPOLLIN)) {
			int sig;
            char signals[1024];
			//�ӹܵ����˶����ź�ֵ���ɹ������ֽ�����ʧ��-1
			//���������ret����ֵ����1��ֻ��14��15����ASCII���Ӧ���ַ�
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

		//����ͻ����Ͻ��յ�������
		else if (events[i].events&EPOLLIN) {
			//�����Ӧ������,����sockfd���ļ�������������ļ���������users�Ǵ�����65535��http_conn����
			//֮���Դ��������Ķ��󣬾��������е�sockfd�ж�����Խӹܣ��ļ���������0-65535�����ļ����������Ӧ��users���������
			util_timer *timer = users_timer[sockfd].timer;
			if (users[sockfd].read_once()) {
				LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                Log::get_instance()->flush();
				//��⵽���¼��������������
				pool->append(users + sockfd);
				//�������ݴ��䣬�򽫶�ʱ�������ӳ�3����λ
				//�����µĶ�ʱ���������ϵ�λ�ý��е���
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
					//�������ر�����
					timer->cb_func(&users_timer[sockfd]);
					if(timer){
						timer_lst.del_timer(timer);
					}
				}
			}

		//����ͻ������Ͻ��յ�������
		else if (events[i].events & EPOLLOUT)
		{
			util_timer *timer = users_timer[sockfd].timer;
			if (users[sockfd].write())
			{
				LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
				Log::get_instance()->flush();

				//�������ݴ��䣬�򽫶�ʱ�������ӳ�3����λ
				//�����µĶ�ʱ���������ϵ�λ�ý��е���
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
		//�ղ�������˵���Է��Ͽ������ӣ��رշ��������ӣ��Ƴ���Ӧ�Ķ�ʱ��
			timer->cb_func(&users_timer[sockfd]);
			if (timer)
				timer_lst.del_timer(timer);
				}
			}
		}
		//����ʱ��Ϊ�Ǳ����¼����յ��źŲ��������ϴ���
		//��ɶ�д�¼����ٴ���SIGALRM�źţ�����ִ�У�ÿ����ִ��һ��
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
