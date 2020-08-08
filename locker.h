#ifndef LOCKER_H
#define	LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>
//信号量
class Sem {
public:
	Sem() { 
		if (sem_init(&m_sem, 0, 0) != 0)
			throw std::exception();
	}
	Sem(int num) {
		if (sem_init(&m_sem, 0, num) != 0)
			throw std::exception();
	}
	~Sem() {
		sem_destroy(&m_sem);
	}
	bool post() {
		return sem_post(&m_sem) == 0;
	}
	bool wait() {
		return sem_wait(&m_sem) == 0;
			
	}

private:
	sem_t m_sem;
};
//互斥量
class Locker {
public:
	Locker() {
		if (pthread_mutex_init(&m_mutex,NULL) != 0)
			throw std::exception();
	}
	~Locker() {
		pthread_mutex_destroy(&m_mutex);
	}
	bool lock() {
		return pthread_mutex_lock(&m_mutex) == 0;
	}
	bool unlock() {
		return pthread_mutex_unlock(&m_mutex) == 0;
	}
	pthread_mutex_t* get() {
		return &m_mutex;
	}
private:
	pthread_mutex_t m_mutex;
};
//条件变量
class Cond {
public:
	Cond()
	{
		if (pthread_cond_init(&m_cond, NULL) != 0)
		{
			throw std::exception();
		}
	}
	~Cond()
	{
		pthread_cond_destroy(&m_cond);
	}
	//这里传入指针
	bool wait(pthread_mutex_t *m_mutex) {
		int ret = 0;
		ret=pthread_cond_wait(&m_cond, m_mutex);
		return ret == 0;
	}
	bool timewait(pthread_mutex_t *m_mutex,struct timespec t) {
		int ret = 0;
		ret = pthread_cond_timedwait(&m_cond, m_mutex,&t);
		return ret == 0;
	}

	bool signal()
	{
		return pthread_cond_signal(&m_cond) == 0;
	}
	bool broadcast()
	{
		return pthread_cond_broadcast(&m_cond) == 0;
	}

private:
	pthread_cond_t m_cond;
};


#endif