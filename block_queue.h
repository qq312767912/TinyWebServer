/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "locker.h"
using namespace std;

template <class T>
class block_queue {
public:
	//block_queue是阻塞队列（生产者消费者模型共享的队列）
	block_queue(int max_size = 1000) {
		if (max_size <= 0) {
			exit(-1);
		}
		//构造函数创建循环数组
		m_max_size = max_size;
		//循环队列，放资源
		m_array = new T[max_size];
		//这个是共享资源的数量
		m_size = 0;
		m_front = -1;
		m_back = -1;
	}

	void clear(){
		m_mutex.lock();
		m_size=0;
		m_front=-1;
		m_back=-1;
		m_mutex.unlock();
	}

	~block_queue(){
		m_mutex.lock();
		if(m_array!=NULL)
			delete [] m_array;
		m_mutex.unlock();
	}

	bool full(){
		m_mutex.lock();
		if(m_size>=m_max_size){
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}

	bool empty(){
		m_mutex.lock();
		if(m_size==0){
			m_mutex.unlock();
			return true;
		}
		m_mutex.unlock();
		return false;
	}
	//返回队首元素
	bool front(T &value){
		m_mutex.lock();
		if(m_size==0){
			m_mutex.unlock();
			return false;
		}
		value=m_array[m_front];
		m_mutex.unlock();
		return true;
	}
	//返回队尾元素
	bool back(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
	int size() 
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }
    int max_size()
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }


	//循环队列里放的是共享资源，阻塞队列里放的是阻塞线程
	//往循环队列添加元素，需要将所有使用队列的线程先唤醒
	//当有元素push进队列，相当于生产者生产了一个元素
	//若当前没有线程等待条件变量，则唤醒无意义

	//bool值表示添加是否成功
	bool push(const T &item) {
		m_mutex.lock();
		//资源过多，先唤醒所有线程，再解锁资源让其竞争
		if (m_size >= m_max_size) {
			m_cond.broadcast();
			m_mutex.unlock();
			return false;
		}

		//将新增资源放在循环数组的末尾
		m_back = (m_back + 1) % m_max_size;
		m_array[m_back] = item;
		m_size++;

		m_cond.broadcast();
		m_mutex.unlock();

		return true;
	}

	//pop时，如果当前队列没有元素，将会等待条件变量
	//这种情况是因为多个线程同时竞争到了资源，结果发现资源已经被用完了
	bool pop(T &item) {
		m_mutex.lock();
		//多个消费者的时候，这里要用while而不是if
		while (m_size <= 0) {
			//当重新抢到互斥锁，wait返回0
			if (!m_cond.wait(m_mutex.get())) {
				m_mutex.unlock();
				return false;
			}
		}

		//取出循环队列首的元素,其实东西还在数组里，只是指针不再指向它，用不了了
		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;
		m_mutex.unlock();
		return true;
	}

	//增加了超时处理，项目中没有用到
	//增加了等待时间，指定时间内能抢到互斥锁即可
	
	bool pop(T &item, int ms_timeout) {
		struct timespec t = { 0,0 };
		struct timeval now = { 0,0 };
		//使用C语言编写程序需要获得当前精确时间（1970年1月1日到现在的时间）
		gettimeofday(&now, NULL);
		m_mutex.lock();
		if (m_size <= 0) {
			//1970到现在经过的秒+限制的秒数，放在t里，t里是绝对时间          
			t.tv_sec = now.tv_sec + ms_timeout / 1000;
			t.tv_nsec = (ms_timeout % 1000) * 1000;
			if (!m_cond.timewait(m_mutex.get(), t)) {
				m_mutex.unlock();
				return false;
			}
		}

		if (m_size <= 0) {
			m_mutex.unlock();
			return false;
		}

		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;
		m_mutex.unlock();
		return true;
	}
private:
    Locker m_mutex;
    Cond m_cond;

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;

};


#endif
