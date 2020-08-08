#ifndef TIMER
#define TIMER

#include <time.h>
#include "log.h"

//������Դ�Ľṹ���Ա��Ҫ�õ���ʱ����
//������Ҫǰ������
class util_timer;

//������Դ�ṹ��
struct client_data {
	//�ͻ���socket��ַ���ļ��������Ͷ�ʱ��
	sockaddr_in address;
	int sockfd;
	util_timer *timer;
};

//��ʱ����
class util_timer {
public:
	util_timer():prev(NULL),next(NULL){}
public:
	//��ʱʱ��
	time_t expire;
	//�ص�����,��������ǵ���ʱ��ʹ��ں��¼���ɾ���¼����ر�fd���ͷ�������Դ
	void(*cb_func)(client_data*);
	//������Դ
	client_data *user_data;
	//ǰ������ʱ��
	util_timer *prev;
	util_timer *next;
};

//��ʱ��������
class sort_timer_lst {
public:
	sort_timer_lst():head(NULL),tail(NULL){}
	//���������������ͷ��ʼɾ
	~sort_timer_lst() {
		util_timer *tmp = head;
		while (tmp) {
			head = tmp->next;
			delete tmp;//ɾ����tmpָ����ڴ棬tmpָ�뻹��
			tmp = head;
		}
	}

	//��Ӷ�ʱ�����ڲ�����˽�г�Աadd_timer
	void add_timer(util_timer *timer) {
		if (!timer)
			return;
		//���ͷβ�ڵ㲻���ڣ���timer����������ָ��
		if (!head) {
			head = tail = timer;
			return;
		}
		//����µĶ�ʱ����ʱʱ��С�ڵ�ǰͷ���ĳ�ʱʱ��
		//ֱ�ӽ���ǰ��ʱ���ڵ���Ϊhead
		if (timer->expire < head->expire) {
			timer->next = head;
			head->prev = timer;
			head = timer;
			return;
		}
		//�������˽�г�Ա�������ڲ��ڵ㵽���ʵ�λ��
		add_timer(timer, head);
	}

	//������ʱ�����������仯ʱ��������ʱ���������е�λ��
	void adjust_timer(util_timer* timer) {
		if (!timer)
			return;
		util_timer *tmp = timer->next;
		//����������Ķ�ʱ����β�����߳�ʱֵ��ȻС����һ����ʱ���ĳ�ʱֵ��������
		if (!tmp || (timer->expire < tmp->expire))
			return;
	
	//��������ʱ��������ͷ��㣬����ȡ�������²���
	if (timer == head) {
		head = head->next;
		head->prev = NULL;
		timer->next = NULL;
		add_timer(timer, head);
	}
	//��������ʱ�����ڲ�������ʱ��ȡ�������²���
	else {
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		add_timer(timer, timer->next);
		}
	}
	//ɾ����ʱ��
	void del_timer(util_timer* timer) {
		if (!timer)
			return;
		//���������ֻ��һ����ʱ��
		if ((timer == head) && (timer == tail)) {
			delete timer;
			head = NULL;
			tail = NULL;
			return;
		}
		//��ɾ���Ķ�ʱ��Ϊͷ���
		if (timer == head) {
			head = head->next;
			head->prev = NULL;
			delete timer;
			return;
		}
		//��ɾ���Ľڵ�Ϊβ���
		if (timer == tail) {
			tail = tail->prev;
			tail->next = NULL;
			delete timer;
			return;
		}
		//��ɾ���Ľڵ��������ڲ�������ɾ��
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		delete timer;
	}
//��ʱ��������,ÿ�յ�SIGALRMִ��һ�Σ������ǰû�ж�ʱ����ʱ������������һ���Դ��������г�ʱ�Ķ�ʱ��
void tick(){
	if (!head)
		return;
	//��ȡ��ǰʱ��
	LOG_INFO("%s", "timer tick");
	Log::get_instance()->flush();
	time_t cur = time(NULL);
	util_timer* tmp = head;

	//������ʱ�������ҳ�ʱ�Ķ�ʱ��
	while (tmp) {
		//�����ǰʱ��С��head�ĳ�ʱʱ�䣬˵����û�е��ڵĶ�ʱ��
		if (cur < tmp->expire)
			break;
		//��ǰ��ʱ�����ڣ����ûص�����
		tmp->cb_func(tmp->user_data);
	
		//�������Ķ�ʱ��������ɾ��������ͷ���
		head = tmp->next;
		if (head)
			head->prev = NULL;
		delete tmp;
		//��仰��Ϊ��һ���Դ��������г�ʱ�Ķ�ʱ��
		tmp = head;
	
	}
}
private:
	//˽�г�Ա�����������г�Աadd_timer��adjust_timerʹ��
	//���ڵ���������λ�õ������ڲ��ڵ�
	void add_timer(util_timer *timer, util_timer *lst_head) {
		//����tmp�Ǹò����λ�ã�prev��tmp��ǰ��
		util_timer* prev = lst_head;
		util_timer* tmp = prev->next;
		//������ǰ�ڵ�֮����������ճ�ʱʱ���ҵ�Ŀ�궨ʱ����Ӧ��λ��
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
		//�����귢��timer��expire������,timer���ڽ�β
		if (!tmp) {
			prev->next = timer;
			timer->prev = prev;
			timer->next = NULL;
			tail = timer;
		}
	}
	//ͷβ�ڵ�
private:
    util_timer *head;
    util_timer *tail;

};


#endif
