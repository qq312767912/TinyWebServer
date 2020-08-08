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

//初始化
connection_pool::connection_pool() {
	this->CurConn = 0;
	this->FreeConn = 0;
}

connection_pool *connection_pool::GetInstance() {
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string user, string password, string dbName, int port, unsigned int maxConn) {
	//初始化数据库信息
	this->url = url;
	this->port = port;
	this->user = user;
	this->passWord = password;
	this->dbName = dbName;

	lock.lock();
	//创建maxConn条数据库连接
	for (int i = 0; i < maxConn; ++i) {
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL) {
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		//连接到数据库服务器
		con = mysql_real_connect(con, url.c_str(), user.c_str(), passWord.c_str(), dbName.c_str(), port, NULL, 0);

		if (con == NULL) {
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		//更新连接池和空闲连接数量
		connList.push_back(con);
		++FreeConn;
	}
	//将信号量初始化为最大连接次数
	reserve = Sem(FreeConn);
	//最后再给最大连接次数赋值
	this->maxConn = FreeConn;
	lock.unlock();
}

//获取连接，使用信号量实现多线程竞争连接的同步机制
MYSQL *connection_pool::GetConnection() {
	MYSQL *con = NULL;
	if (connList.size() == 0)
		return NULL;
	//取出连接，信号量原子-1.为0则等待
	reserve.wait();
	lock.lock();
	con = connList.front();
	connList.pop_front();

	//这里的两个变量并没有用到
	--FreeConn;
	++CurConn;

	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con) {
	if (con == NULL)
		return false;
	lock.lock();
	connList.push_back(con);
	++FreeConn;
	--CurConn;

	lock.unlock();
	//释放连接信号量+1
	reserve.post();
	return true;
}

//销毁连接池,通过迭代器遍历，关闭对应数据库连接，清空链表并重置空闲连接和现有连接数量
void connection_pool::DestroyPool() {
	lock.lock();
	if (connList.size() > 0) {
		//通过迭代器遍历，关闭数据库连接
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it) {
			MYSQL *con = *it;
			mysql_close(con);
		}
		CurConn = 0;
		FreeConn = 0;

		//清空list
		connList.clear();
		lock.unlock();
	}
	lock.unlock();
}

int connection_pool::GetFreeConn() {
	return this->FreeConn;
}

//RAII机制销毁连接池
connection_pool::~connection_pool() {
	DestroyPool();
}

//RAII机制释放数据库连接
//将数据库连接的获取与释放通过RAII机制封装，避免手动释放
//在获取连接时，通过有参构造对传入的参数进行修改。其中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。


//拿一个空的连接去初始化
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool) {
	*SQL = connPool->GetConnection();
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII() {
	poolRAII->ReleaseConnection(conRAII);
}
