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
	//�ļ���������out���򿪲���2���ļ�
	ifstream out(argv[2]);
	string linestr;
	//out��ʾ��������linestr�б����out���ȡ����Ϣ
	while (getline(out, linestr))
	{
		string str;
		stringstream id_passwd(linestr);
		//�ո��ʾ�ض��ַ� ������ֹͣ���뵽str
		getline(id_passwd, str, ' ');
		string temp1(str);

		getline(id_passwd, str, ' ');
		string temp2(str);
		users[temp1] = temp2;
	}
	out.close();

	//ֻ��ɵ�¼
	string name(argv[0]);
	const char *namep = name.c_str();
	string passwd(argv[1]);
	const char *passwdp = passwd.c_str();

	if (users.find(name) != users.end() && users[name] == passwd)
		printf("1\n");
	else
		printf("0\n");

}