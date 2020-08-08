#include <mysql/mysql.h>
#include <iostream>
#include <string>
#include <string.h>
#include <cstdio>
#include "sql_connection_pool.h"
#include <map>
#include <fstream>
#include <sstream>
#include"locker.h"

using namespace std;

int main(int argc, char *argv[])
{
	map<string, string> users;

	Locker lock;
	//文件读操作类out，打开参数2的文件
	ifstream out(argv[2]);
	string linestr;
	//out表示输入流，linestr中保存从out里读取的信息
	while (getline(out, linestr))
	{
		string str;
		stringstream id_passwd(linestr);
		//空格表示截断字符 遇到即停止输入到str
		getline(id_passwd, str, ' ');
		string temp1(str);

		getline(id_passwd, str, ' ');
		string temp2(str);
		users[temp1] = temp2;
	}
	out.close();

	//只完成登录
	string name(argv[0]);
	const char *namep = name.c_str();
	string passwd(argv[1]);
	const char *passwdp = passwd.c_str();

	if (users.find(name) != users.end() && users[name] == passwd)
		printf("1\n");
	else
		printf("0\n");

}