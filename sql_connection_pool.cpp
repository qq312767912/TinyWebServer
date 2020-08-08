#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

//��ʼ��
connection_pool::connection_pool() {
	this->CurConn = 0;
	this->FreeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
	static connection_pool connPool;
	return &connPool;
}

//�����ʼ��
void connection_pool::init(string url, string user, string password, string dbName, int port, unsigned int maxConn) {
	//��ʼ�����ݿ���Ϣ
	this->url = url;
	this->port = port;
	this->user = user;
	this->passWord = password;
	this->dbName = dbName;

	lock.lock();
	//����maxConn�����ݿ�����
	for (int i = 0; i < maxConn; ++i) {
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL) {
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		//���ӵ����ݿ������
		con = mysql_real_connect(con, url.c_str(), user.c_str(), passWord.c_str(), dbName.c_str(), port, NULL, 0);

		if (con == NULL) {
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		//�������ӳغͿ�����������
		connList.push_back(con);
		++FreeConn;
	}
	//���ź�����ʼ��Ϊ������Ӵ���
	reserve = Sem(FreeConn);
	//����ٸ�������Ӵ�����ֵ
	this->maxConn = FreeConn;
	lock.unlock();
}

//��ȡ���ӣ�ʹ���ź���ʵ�ֶ��߳̾������ӵ�ͬ������
MYSQL *connection_pool::GetConnection() {
	MYSQL *con = NULL;
	if (connList.size() == 0)
		return NULL;
	//ȡ�����ӣ��ź���ԭ��-1.Ϊ0��ȴ�
	reserve.wait();
	lock.lock();
	con = connList.front();
	connList.pop_front();

	//���������������û���õ�
	--FreeConn;
	++CurConn;

	lock.unlock();
	return con;
}

//�ͷŵ�ǰʹ�õ�����
bool connection_pool::ReleaseConnection(MYSQL *con) {
	if (con == NULL)
		return false;
	lock.lock();
	connList.push_back(con);
	++FreeConn;
	--CurConn;

	lock.unlock();
	//�ͷ������ź���+1
	reserve.post();
	return true;
}

//�������ӳ�,ͨ���������������رն�Ӧ���ݿ����ӣ�����������ÿ������Ӻ�������������
void connection_pool::DestroyPool() {
	lock.lock();
	if (connList.size() > 0) {
		//ͨ���������������ر����ݿ�����
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it) {
			MYSQL *con = *it;
			mysql_close(con);
		}
		CurConn = 0;
		FreeConn = 0;

		//���list
		connList.clear();
		lock.unlock();
	}
	lock.unlock();
}

int connection_pool::GetFreeConn() {
	return this->FreeConn;
}

//RAII�����������ӳ�
connection_pool::~connection_pool() {
	DestroyPool();
}

//RAII�����ͷ����ݿ�����
//�����ݿ����ӵĻ�ȡ���ͷ�ͨ��RAII���Ʒ�װ�������ֶ��ͷ�
//�ڻ�ȡ����ʱ��ͨ���вι���Դ���Ĳ��������޸ġ��������ݿ����ӱ�����ָ�����ͣ����Բ�����Ҫͨ��˫ָ����ܶ�������޸ġ�


//��һ���յ�����ȥ��ʼ��
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
	*SQL = connPool->GetConnection();
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
	poolRAII->ReleaseConnection(conRAII);
}
