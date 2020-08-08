#ifndef TIMER
#define TIMER

#include <time.h>
#include "log.h"

//连接资源的结构体成员需要用到定时器类
//这里需要前向声明
class util_timer;

//连接资源结构体
struct client_data {
	//客户端socket地址、文件描述符和定时器
	sockaddr_in address;
	int sockfd;
	util_timer *timer;
};

//定时器类
class util_timer {
public:
	util_timer():prev(NULL),next(NULL){}
public:
	//超时时间
	time_t expire;
	//回调函数,具体操作是到了时间就从内核事件表删除事件，关闭fd，释放连接资源
	void(*cb_func)(client_data*);
	//连接资源
	client_data *user_data;
	//前、后向定时器
	util_timer *prev;
	util_timer *next;
};

//定时器容器类
class sort_timer_lst {
public:
	sort_timer_lst():head(NULL),tail(NULL){}
	//常规的销毁链表，从头开始删
	~sort_timer_lst() {
		util_timer *tmp = head;
		while (tmp) {
			head = tmp->next;
			delete tmp;//删掉了tmp指向的内存，tmp指针还在
			tmp = head;
		}
	}

	//添加定时器，内部调用私有成员add_timer
	void add_timer(util_timer *timer) {
		if (!timer)
			return;
		//如果头尾节点不存在，把timer赋给这两个指针
		if (!head) {
			head = tail = timer;
			return;
		}
		//如果新的定时器超时时间小于当前头结点的超时时间
		//直接将当前定时器节点作为head
		if (timer->expire < head->expire) {
			timer->next = head;
			head->prev = timer;
			head = timer;
			return;
		}
		//否则调用私有成员，调整内部节点到合适的位置
		add_timer(timer, head);
	}

	//调整定时器，任务发生变化时，调整定时器在链表中的位置
	void adjust_timer(util_timer* timer) {
		if (!timer)
			return;
		util_timer *tmp = timer->next;
		//如果被调整的定时器在尾部或者超时值仍然小于下一个定时器的超时值，不调整
		if (!tmp || (timer->expire < tmp->expire))
			return;
	
	//被调整定时器是链表头结点，则将它取出，重新插入
	if (timer == head) {
		head = head->next;
		head->prev = NULL;
		timer->next = NULL;
		add_timer(timer, head);
	}
	//被调整定时器在内部，将定时器取出，重新插入
	else {
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		add_timer(timer, timer->next);
		}
	}
	//删除定时器
	void del_timer(util_timer* timer) {
		if (!timer)
			return;
		//如果链表中只有一个定时器
		if ((timer == head) && (timer == tail)) {
			delete timer;
			head = NULL;
			tail = NULL;
			return;
		}
		//被删除的定时器为头结点
		if (timer == head) {
			head = head->next;
			head->prev = NULL;
			delete timer;
			return;
		}
		//被删除的节点为尾结点
		if (timer == tail) {
			tail = tail->prev;
			tail->next = NULL;
			delete timer;
			return;
		}
		//被删除的节点在链表内部，常规删除
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		delete timer;
	}
//定时任务处理函数,每收到SIGALRM执行一次，如果当前没有定时器超时则跳出，否则一次性处理完所有超时的定时器
void tick(){
	if (!head)
		return;
	//获取当前时间
	LOG_INFO("%s", "timer tick");
	Log::get_instance()->flush();
	time_t cur = time(NULL);
	util_timer* tmp = head;

	//遍历定时器链表找超时的定时器
	while (tmp) {
		//如果当前时间小于head的超时时间，说明还没有到期的定时器
		if (cur < tmp->expire)
			break;
		//当前定时器到期，调用回调函数
		tmp->cb_func(tmp->user_data);
	
		//将处理后的定时器从链表删除，重置头结点
		head = tmp->next;
		if (head)
			head->prev = NULL;
		delete tmp;
		//这句话是为了一次性处理完所有超时的定时器
		tmp = head;
	
	}
}
private:
	//私有成员函数，被公有成员add_timer和adjust_timer使用
	//用于调整非特殊位置的链表内部节点
	void add_timer(util_timer *timer, util_timer *lst_head) {
		//假设tmp是该插入的位置，prev是tmp的前驱
		util_timer* prev = lst_head;
		util_timer* tmp = prev->next;
		//遍历当前节点之后的链表，按照超时时间找到目标定时器对应的位置
		while (tmp) {
			if (timer->expire < tmp->expire) {
				prev->next = timer;
				timer->next = tmp;
				tmp->prev = timer;
				timer->prev = prev;
				break;
			}
			prev = tmp;
			tmp = tmp->next;
		}
		//遍历完发现timer的expire是最大的,timer放在结尾
		if (!tmp) {
			prev->next = timer;
			timer->prev = prev;
			timer->next = NULL;
			tail = timer;
		}
	}
	//头尾节点
private:
    util_timer *head;
    util_timer *tail;

};


#endif
