#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include "locker.h"
#include "sql_connection_pool.h"

class http_conn {
public:
	//���ö�ȡ�ļ�������
	��С
	static const int FILENAME_LEN = 200;
	//���ö�������m_read_buf��С
	static const int READ_BUFFER_SIZE = 2048;
	//����д������m_write_buf��С
	static const int WRITE_BUFFER_SIZE = 1024;
	//���ĵ����󷽷�
	enum METHOD {
		GET = 0,
		POST
	};
	//��״̬����״̬
	enum CHECK_STATE {
		CHECK_STATE_REQUESTLINE = 0,
		CHECK_STATE_HEADER,
		CHECK_STATE_CONTENT
	};
	//���Ľ������
	enum HTTP_CODE {
		NO_REQUEST,//�����в�����
		GET_REQUEST,//�����������http����
		BAD_REQUEST,//http�������﷨����
		NO_RESOURCE,
		FORBIDDEN_REQUEST,
		FILE_REQUEST,//��Դ����
		INTERNAL_ERROR,//�������ڲ�����
		CLOSED_CONNECTION
	};
	//��״̬��״̬
	enum LINE_STATUS {
		LINE_OK=0,
		LINE_BAD,
		LINE_OPEN
	};

	http_conn(){}
	~http_conn(){}
	//��ʼ���׽��ֵ�ַ�������ڲ������˽�з���init
	void init(int sockfd, const sockaddr_in &addr);
	//�ر�http����
	void close_conn(bool real_close = true);
	//
	void process();
	//��ȡ������˷����������ģ�ֱ�������ݿɶ���Է��ر����ӣ���ȡ��
	//m_read_buffer�У�������m_read_idx
	bool read_once();
	//��Ӧ����д�뺯��
	bool write();
	sockaddr_in *get_address() {
		return &m_address;
	}
	//ͬ���̳߳�ʼ�����ݿ��ȡ������ȡһ
	void initmysql_result(connection_pool *connPool);
	//CGIʹ�����ӳس�ʼ�����ݿ��
	void initresultFile(connection_pool *connPool);

private:
	//��ʼ��HTTP����
	void init();
	//��m_read_buf��ȡ��������������
	HTTP_CODE process_read();
	//��m_write_bufд����Ӧ��������
	bool process_write(HTTP_CODE ret);
	//��״̬�����������е�����������
	HTTP_CODE parse_request_line(char* text);
	//��״̬�����������е�����ͷ
	HTTP_CODE parse_headers(char* text);
	//��״̬���������ĵ���������
	HTTP_CODE parse_content(char* text);
	//������Ӧ����
	HTTP_CODE do_request();

	//m_start_line���Ѿ��������ַ�����һ���ֽڵĵ�ַ��������buffer�е���ʼλ�ã�����λ�õ���һ��λ�����ݸ��Ƹ�text
	//��ʱ��״̬���Ѿ���ǰ��һ�е�ĩβ�ַ�\r\n��Ϊ\0\0������text����ֱ��ȡ���������н��н���
	//get_line���ڽ�ָ�����ƫ�ƣ�ָ��δ������ַ�
	char* get_line() {
		return m_read_buf + m_start_line;
	};
	//��״̬������һ�У��������������ĵ��Ĳ���
	LINE_STATUS parse_line();
	void unmap();//?????????

	//������Ӧ���ĸ�ʽ�����ɶ�Ӧ8�����֣����º�������do_request����
	bool add_response(const char *format, ...);
	bool add_content(const char *content);
	bool add_status_line(int status, const char *title);
	bool add_headers(int content_length);
	bool add_content_type();
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	static int m_epollfd;
	static int m_user_count;
	MYSQL *mysql;

private:
	int m_sockfd;
	sockaddr_in m_address;

	//�洢��ȡ�����������ݣ���������
	char m_read_buf[READ_BUFFER_SIZE];
	//�������������ݵ����һ���ֽڵ���һ��λ��,�����ж������Ƿ񱻶���
	int m_read_idx;
	//���������Ѿ���ȡ��λ�ã���״̬����m_read_buf�ж�ȡ��λ��
	int m_checked_idx;
	//�����������Ѿ��������ַ�����
	int m_start_line;

	//�洢��������Ӧ��������
	char m_write_buf[WRITE_BUFFER_SIZE];
	//ָʾbuffer�еĳ���
	int m_write_idx;

	//��״̬����״̬
	CHECK_STATE m_check_state;
	//���󷽷�
	METHOD m_method;

	//����Ϊ�����������ж�Ӧ��6������
	char m_real_file[FILENAME_LEN];
	char *m_url;
	char *m_version;
	char *m_host;
	int m_content_length;
	bool m_linger;

	char *m_file_address;   //��ȡ�������ϵ��ļ���ַ
	struct stat m_file_stat;//stat�ṹ�壬������Դ����
	struct iovec m_iv[2];   //io��������iovec,��һ��ָ��ָ����Ӧ���Ļ�����������ָ��idx;�ڶ���ָ��ָ��mmap���ص��ļ�ָ�룬����ָ���ļ���С
	int m_iv_count;
	int cgi;                //�Ƿ����õ�POST
	char *m_string;         //�洢����ͷ����
	int bytes_to_send;      //ʣ�෢���ֽ���
	int bytes_have_send;    //�ѷ����ֽ���


};


#endif
