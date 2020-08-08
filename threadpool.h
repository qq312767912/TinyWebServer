#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"
#include "sql_connection_pool.h"

template <typename T>
class ThreadPool {
public:
	ThreadPool(connection_pool *connPool, int thread_number = 8, int max_request = 10000);
	~ThreadPool();

	//向请求队列插入请求
	bool append(T* request);

private:
	//工作线程的运行函数，符合线程函数的模板
	static void *worker(void* arg);
	//其实是在worker里调用run
	void run();

	int m_thread_number;
	int m_max_request;
	//描述线程池的数组
	pthread_t *m_threads;
	//请求队列
	std::list<T *>m_requestQueue;
	//保护请求队列的互斥锁
	Locker m_queueLocker;
	//请求队列的信号量
	Sem m_queueStat;
	//是否结束线程
	bool m_stop;
	//数据库连接池
	connection_pool *m_connPool;
};

template<typename T>
ThreadPool<T>::ThreadPool(connection_pool *connPool, int thread_number, int max_request) :m_thread_number(thread_number), m_max_request(max_request), m_stop(false), m_threads(NULL), m_connPool(connPool) {
	if (thread_number <= 0 || max_request <= 0)
		throw std::exception();
	
	m_threads = new pthread_t[m_thread_number];
	if (!m_threads)
		throw std::exception();

	for (int i = 0; i < thread_number; ++i) {
		if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
			delete[] m_threads;
			throw std::exception();
		}

		if (pthread_detach(m_threads[i])) {
			delete[] m_threads;
			throw std::exception();
		}
	}
}

template <typename T>
ThreadPool<T>::~ThreadPool()
{
	delete[] m_threads;
	m_stop = true;
}


template<typename T>
bool ThreadPool<T>::append(T* request) {
	m_queueLocker.lock();
	if (m_requestQueue.size() > m_max_request) {
		m_queueLocker.unlock();
		return false;
	}
	m_requestQueue.push_back(request);
	m_queueLocker.unlock();

	m_queueStat.post();
	return true;

}


template<typename T>
void *ThreadPool<T>::worker(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	pool->run();
	return pool;
}


template<typename T>
void ThreadPool<T>::run() {
	while (!m_stop) {
		m_queueStat.wait();
		m_queueLocker.lock();
		if (m_requestQueue.empty()) {
			m_queueLocker.unlock();
			continue;
		}

		//从请求队列取出第一个请求
		T *request = m_requestQueue.front();
		m_requestQueue.pop_front();
		m_queueLocker.unlock();
		if (!request)
			continue;
		//从连接池取出一个数据库连接
		//request->mysql = m_connPool->GetConnection();
		
		connectionRAII mysqlcon(&request->mysql, m_connPool);
		
		//process(模板类中的方法，这里是http类)进行处理
		request->process();
		//释放数据库连接
		//m_connPool->ReleaseConnection(request->mysql);
	}
}


#endif
