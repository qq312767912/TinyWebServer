#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log {
public:
	//C++11�Ժ�ʹ�þֲ������������ü���
	static Log *get_instance() {
		static Log instance;
		return &instance;
	}

	//��ѡ��Ĳ�������־�ļ�������־��������С(Ĭ��8M��������������־������
	bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
	//�첽д��־���з���������˽��async_write_log
	static void* flush_log_thread(void *args) {
		Log::get_instance()->async_write_log();
	}
	//��������ݰ��ձ�׼��ʽ����
	void write_log(int level, const char *format, ...);
	//ǿ��ˢ�»�����
	void flush(void);

private:
	Log();
	//Ϊʲô������������������
	virtual ~Log();

	//�첽д��־����
	void *async_write_log(){
		string single_log;
		//������������ȡ��һ����־���ݣ�д���ļ�
		while (m_log_queue->pop(single_log)) {
			m_mutex.lock();
			fputs(single_log.c_str(), m_fp);
			m_mutex.unlock();
		}
	}
private:
	char dir_name[128];//·����
	char log_name[128];//log�ļ���
	int m_split_lines;//��־�������
	int m_log_buf_size;//��־��������С
	long long m_count;//��־������¼
	int m_today;	//������ļ�����¼��ǰʱ������һ��
	FILE* m_fp;		//��log���ļ�ָ��
	char *m_buf;	//Ҫ���������
	block_queue<string> *m_log_queue; //��������
	bool m_is_async;	//�Ƿ�ͬ����־λ
	Locker m_mutex;		//ͬ����
};

//���ĸ��궨���������ļ���ʹ�ã���Ҫ���ڲ�ͬ���͵���־���
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)



#endif
