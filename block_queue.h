/*************************************************************
*ѭ������ʵ�ֵ��������У�m_back = (m_back + 1) % m_max_size;
*�̰߳�ȫ��ÿ������ǰ��Ҫ�ȼӻ���������������ٽ���
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
	//block_queue���������У�������������ģ�͹���Ķ��У�
	block_queue(int max_size = 1000) {
		if (max_size <= 0) {
			exit(-1);
		}
		//���캯������ѭ������
		m_max_size = max_size;
		//ѭ�����У�����Դ
		m_array = new T[max_size];
		//����ǹ�����Դ������
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
	//���ض���Ԫ��
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
	//���ض�βԪ��
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


	//ѭ��������ŵ��ǹ�����Դ������������ŵ��������߳�
	//��ѭ���������Ԫ�أ���Ҫ������ʹ�ö��е��߳��Ȼ���
	//����Ԫ��push�����У��൱��������������һ��Ԫ��
	//����ǰû���̵߳ȴ���������������������

	//boolֵ��ʾ����Ƿ�ɹ�
	bool push(const T &item) {
		m_mutex.lock();
		//��Դ���࣬�Ȼ��������̣߳��ٽ�����Դ���侺��
		if (m_size >= m_max_size) {
			m_cond.broadcast();
			m_mutex.unlock();
			return false;
		}

		//��������Դ����ѭ�������ĩβ
		m_back = (m_back + 1) % m_max_size;
		m_array[m_back] = item;
		m_size++;

		m_cond.broadcast();
		m_mutex.unlock();

		return true;
	}

	//popʱ�������ǰ����û��Ԫ�أ�����ȴ���������
	//�����������Ϊ����߳�ͬʱ����������Դ�����������Դ�Ѿ���������
	bool pop(T &item) {
		m_mutex.lock();
		//��������ߵ�ʱ������Ҫ��while������if
		while (m_size <= 0) {
			//������������������wait����0
			if (!m_cond.wait(m_mutex.get())) {
				m_mutex.unlock();
				return false;
			}
		}

		//ȡ��ѭ�������׵�Ԫ��,��ʵ�������������ֻ��ָ�벻��ָ�������ò�����
		m_front = (m_front + 1) % m_max_size;
		item = m_array[m_front];
		m_size--;
		m_mutex.unlock();
		return true;
	}

	//�����˳�ʱ������Ŀ��û���õ�
	//�����˵ȴ�ʱ�䣬ָ��ʱ��������������������
	
	bool pop(T &item, int ms_timeout) {
		struct timespec t = { 0,0 };
		struct timeval now = { 0,0 };
		//ʹ��C���Ա�д������Ҫ��õ�ǰ��ȷʱ�䣨1970��1��1�յ����ڵ�ʱ�䣩
		gettimeofday(&now, NULL);
		m_mutex.lock();
		if (m_size <= 0) {
			//1970�����ھ�������+���Ƶ�����������t�t���Ǿ���ʱ��          
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
