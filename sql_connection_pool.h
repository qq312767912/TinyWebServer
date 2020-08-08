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
	MYSQL *GetConnection();					//获取数据库连接
	bool ReleaseConnection(MYSQL *conn);	//释放连接
	int GetFreeConn();						//获取可用连接数
	void DestroyPool();						//销毁所有连接

	//单例模式
	static connection_pool *GetInstance();
	void init(string url, string user, string passwd, string dbName, int port, unsigned int maxConn);
	connection_pool();
	~connection_pool();

private:
	unsigned int maxConn;
	unsigned int CurConn;
	unsigned int FreeConn;

	Locker lock;
	list<MYSQL *> connList;//数据库连接池
	Sem reserve;

	string url;			//主机地址
	string port;		//数据库端口号
	string user;		//登录数据库用户名	
	string passWord;	//登录数据库密码
	string dbName;		//使用数据库名

};

class connectionRAII {
public:
	//双指针对MYSQL *con修改
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
