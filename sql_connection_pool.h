#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "locker.h"

using namespace std;

class connection_pool {
public:
	MYSQL *GetConnection();					//��ȡ���ݿ�����
	bool ReleaseConnection(MYSQL *conn);	//�ͷ�����
	int GetFreeConn();						//��ȡ����������
	void DestroyPool();						//������������

	//����ģʽ
	static connection_pool *GetInstance();
	void init(string url, string user, string passwd, string dbName, int port, unsigned int maxConn);
	connection_pool();
	~connection_pool();

private:
	unsigned int maxConn;
	unsigned int CurConn;
	unsigned int FreeConn;

	Locker lock;
	list<MYSQL *> connList;//���ݿ����ӳ�
	Sem reserve;

	string url;			//������ַ
	string port;		//���ݿ�˿ں�
	string user;		//��¼���ݿ��û���	
	string passWord;	//��¼���ݿ�����
	string dbName;		//ʹ�����ݿ���

};

class connectionRAII {
public:
	//˫ָ���MYSQL *con�޸�
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
