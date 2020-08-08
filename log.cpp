#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log(){
	m_count=0;
	m_is_async=false;
}

Log::~Log(){
	if(m_fp != NULL){
		fclose(m_fp);
	}
}

//init����ʵ����־������д�뷽ʽ���ж�
//ͨ������ģʽ��ȡΨһ����־�࣬����init��������ʼ��������־�ļ�����������������ǰʱ�̴�����־��ǰ׺Ϊʱ�䣬��׺Ϊ�Զ���log�ļ���������¼������־��ʱ��day������count��
//д�뷽ʽͨ����ʼ��ʱ�Ƿ����ö��д�С����ʾ�ڶ����п��Էż������ݣ����жϣ������д�СΪ0����Ϊͬ��������Ϊ�첽��
bool Log::init(const char* file_name, int log_buf_size, int split_lines, int max_queue_size) {
	//���������max_queue_size��Ϊ�첽
	if (max_queue_size >= 1) {
		m_is_async = true;
		//�����������������г��ȣ��õ�����ģ��
		m_log_queue = new block_queue<string>(max_queue_size);
		//Ȼ�󴴽��߳����첽д��־
		pthread_t tid;
		//flush_log_threadΪ�ص�����
		pthread_create(&tid, NULL, flush_log_thread, NULL);
	}

	//��־��������С
	m_log_buf_size = log_buf_size;
	m_buf = new char[m_log_buf_size];
	memset(m_buf, '\0', m_log_buf_size);
	//��־���������
	m_split_lines = split_lines;

	time_t t = time(NULL);
	struct tm *sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;

	//�Ӻ���ǰ�ҵ���һ��/��λ��
	const char *p = strrchr(file_name, '/');
	char log_full_name[256] = { 0 };

	//�൱���Զ�����־��
	//��������ļ���û��/����ֱ�ӽ�ʱ��+�ļ�����Ϊ��־��
	if (p == NULL) {
		snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
	}
	else {
		//��/��λ������ƶ�һ����Ȼ���Ƶ�log_name��
		strcpy(log_name, p + 1);
		//p - file_name + 1���ļ�����·���ļ��еĳ���,��file_name��ȡ��dir_name
		strncpy(dir_name, file_name, p - file_name + 1);
		
		//����Ĳ�����format�й�
		snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
	}

	m_today = my_tm.tm_mday;

	m_fp = fopen(log_full_name, "a");
	if (m_fp == NULL)
		return false;
	return true;
}


//write_log�������д����־�ļ��еľ������ݣ�ʵ����־�ּ������ļ�����ʽ���������
void Log::write_log(int level, const char* format, ...) {
	struct timeval now = { 0,0 };
	//gettimeofday��timeһ�����ǵõ���epoch�����ڵ�ʱ�䣬��gettimeofday��ȷ��΢��
	gettimeofday(&now, NULL);
	time_t t = now.tv_sec;
	struct tm *sys_tm = localtime(&t);
	struct tm my_tm = *sys_tm;
	char s[16] = { 0 };

	//��־�ּ�
	switch (level) {
	case 0:
		strcpy(s, "[debug]:");
		break;
	case 1:
		strcpy(s, "[info]:");
		break;
	case 2:
		strcpy(s, "[warn]:");
		break;
	case 3:
		strcpy(s, "[erro]:");
		break;
	default:
		strcpy(s, "[info]:");
		break;
	}

	m_mutex.lock();

	//������������
	m_count++;

	//��־�����ǽ��죬����д�����־�����ۻ��ﵽ�������������Ϊ������һ�����ӵ�
	//m_split_linesΪ�������
	if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
		char new_log[256] = { 0 };
		fflush(m_fp);
		fclose(m_fp);
		char tail[16] = { 0 };

		//��ʽ����־���е�ʱ�䲿��
		snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
		//���ʱ�䲻�ǽ��죬�򴴽��������־������m_today��m_count
		if (m_today != my_tm.tm_mday) {
			snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
			m_today = my_tm.tm_mday;
			m_count = 0;
		}
		else {
			//����������У���֮ǰ����־�������ϼӺ�׺m_count / m_split_lines
			snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
		}
		m_fp = fopen(new_log, "a");
	}

	m_mutex.unlock();
	va_list valst;
	//�������format������ֵ��valst�����ڸ�ʽ�����
	va_start(valst, format);

	string log_str;
	m_mutex.lock();

	//д�����ݸ�ʽ��ʱ��+����
	//ʱ���ʽ����snprintf�ɹ�����д�ַ������������в�������β��null�ַ�
	int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

	//���ݸ�ʽ�����������ַ����д�ӡ���ݡ����ݸ�ʽ���û��Զ��壬����д�뵽�ַ�����str�е��ַ�������������ֹ��
	int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
	m_buf[n + m] = '\n';
	m_buf[n + m + 1] = '\0';

	log_str = m_buf;
	m_mutex.unlock();

	//m_is_asyncΪtrue��ʾ�첽��Ĭ��Ϊͬ��
	//���첽������־��Ϣ�����������У�ͬ����������ļ���д
	if (m_is_async && !m_log_queue->full()) {
		m_log_queue->push(log_str);	//������������string����
	}
	else {
		m_mutex.lock();
		//��m_fp�ļ�д��log_str
		fputs(log_str.c_str(), m_fp);
		m_mutex.unlock();
	}
	va_end(valst);
}

void Log::flush(void){
	m_mutex.lock();
	//ǿ��ˢ��д����������
	fflush(m_fp);
	m_mutex.unlock();
}
