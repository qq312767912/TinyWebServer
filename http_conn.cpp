#include "http_conn.h"
#include "log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>

//同步校验
#define SYNSQL

//#define ET       //边缘触发非阻塞
#define LT         //水平触发阻塞

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//网站的根目录，文件夹内存放请求的资源和跳转的html文件
//当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
const char *doc_root = "/home/boystory/webServer/root";

//将表中的用户名和密码放入map
map<string, string> users;
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

#ifdef SYNSQL

void http_conn::initmysql_result(connection_pool *connPool){
	//先从连接池中取一个连接
	MYSQL *mysql = NULL;
	connectionRAII mysqlcon(&mysql,connPool);
	//在user表中检索username，paddwd，浏览器端输入，返回空表示成功
	if(mysql_query(mysql,"SELECT username,passwd FROM user")){
		LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));
	}

	//从表中检索完整的结果表
	MYSQL_RES *result = mysql_store_result(mysql);
	//返回结果集中的列数
	int num_fields = mysql_num_fields(result);
	//返回所有字段结构的数组
	MYSQL_FIELD *fields = mysql_fetch_fields(result);
	//从结果集中获取下一行，将对应的用户名和密码存入map中
	while(MYSQL_ROW row = mysql_fetch_row(result)){
		string temp1(row[0]);
		string temp2(row[1]);
		users[temp1]=temp2;
	}
}
#endif

#ifdef CGISQLPOOL


void http_conn::initresultFile(connection_pool *connPool)
{
    ofstream out("./CGImysql/id_passwd.txt");
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        out << temp1 << " " << temp2 << endl;
        users[temp1] = temp2;
    }

    out.close();
}

#endif

//一次性读完数据
bool http_conn::read_once() {
	//如果缓冲区满了
	if (m_read_idx >= READ_BUFFER_SIZE) {
		return false;
	}
	int bytes_read = 0;
	while (1) {
		//m_read_idx初始值应该是0
		bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
		if (bytes_read == -1) {
			//recv是阻塞式的，事件未发生则返回-1且errno设为EAGAIN
			//边沿触发方式中，接收数据时仅注册一次该事件。因此一旦发生输入，就要读取输入缓冲区中的全部数据，所以要验证输入缓冲区是否为空
			//EWOULDBLOCK和EAGAIN是同一个东西，如果此时返回-1和设置了EAGAIN则说明读完了，没有事件发生
			//这是专为边沿触发写的if
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break;
			}
			return false;
		}
		else if (bytes_read == 0)//表示读不到数据，正常情况下不会出现这个情况
			return false;
		m_read_idx += bytes_read;
	}
	return true;
}

//设置非阻塞
int setnonblock(int fd) {
	int old_option = fcntl(fd, F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd, F_SETFL, new_option);
	return old_option;
}

void addfd(int epollfd, int fd, bool one_shot) {
	epoll_event event;
	event.data.fd = fd;
	// EPOLLRDHUP 事件，代表对端断开连接
#ifdef ET
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef LT
	event.events = EPOLLIN | EPOLLRDHUP;
#endif

	if (one_shot)
		event.events |= EPOLLONESHOT;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblock(fd);
}

//初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    //int reuse=1;
    //setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
	//内部初始化http对象
    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}


//内核事件表删除事件
void removefd(int epollfd,int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

//重置EPOLLONESHOT
void modfd(int epollfd, int fd, int ev) {
	epoll_event event;
	event.data.fd = fd;

#ifdef ET
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef LT
	event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

	epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//process是整体上的处理，先解析报文，再响应报文
void http_conn::process() {
	//process_read里do_request
	HTTP_CODE read_ret = process_read();
	//NO_REQUEST，表示请求不完整，需要继续接收请求数据
	if (read_ret == NO_REQUEST) {
		//注册并监听读事件
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		return;
	}
	//调用process_write完成报文响应
	bool write_ret = process_write(read_ret);
	if (!write_ret) {
		close_conn();
	}
	//注册并监听写事件
	modfd(m_epollfd, m_sockfd,EPOLLOUT);
}

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


//process_read封装了主从状态机，对整个报文进行循环处理
//m_start_line是行在buffer中的起始位置，将该位置的后一字节数据地址赋给text
//此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
http_conn::HTTP_CODE http_conn::process_read() {
	LINE_STATUS line_status = LINE_OK;
	HTTP_CODE ret = NO_REQUEST;
	char *text = 0;
	//这句有循环执行line_status=parse_line()，成功解析出来了一行（请求行、头、内容、空行）才能接着走
	while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
	{
		text = get_line();
		//m_start_line是每个数据行在读缓冲区中的位置，是主状态机用的变量，其实和下面从状态机用的变量是同一个东西
		//m_checked_idx表示从状态机在读缓冲区中读取的位置
		m_start_line = m_checked_idx;

		LOG_INFO("%s", text);
		Log::get_instance()->flush();
		
		switch (m_check_state)
		{
		case CHECK_STATE_REQUESTLINE:
		{
			//主状态机解析的时候，数据已经预处理过了，空行改为\0\0
			//判断各部分是否完整靠主状态机的HTTP_CODE跳转，状态跳转被包含在各部分的解析函数里
			ret = parse_request_line(text);
			if (ret == BAD_REQUEST)
				return BAD_REQUEST;
			break;
		}
		case CHECK_STATE_HEADER:
		{
			ret = parse_headers(text);
			if (ret == BAD_REQUEST)
				return BAD_REQUEST;
			//完整地解析了GET请求后，跳转到报文响应函数
			else if (ret == GET_REQUEST)
			{
				//如果获取了完整的get报文，那么就返回请求的资源
				return do_request();
			}
			break;
		}
		case CHECK_STATE_CONTENT:
		{
			ret = parse_content(text);
			//完整解析了POST请求后，跳转到资源获取函数
			if (ret == GET_REQUEST)
				return do_request();
			//消息体被访问完毕之后应该不会再有内容了，可能有空行，但是不必进入循环了
			//此时如果还没获取完整的消息体，说明消息体有问题，就手动设置跳出循环，返回默认的NO_REQUEST；
			//如果有消息内容，那么需要把行状态改为LINE_OPEN来退出循环，GET方法是没有消息内容的
			line_status = LINE_OPEN;
			break;
		}
		default:
			return INTERNAL_ERROR;
		}
	}
	//默认返回消息不完整
	return NO_REQUEST;
}


//从状态机，用于分析出一行内容，返回值包括LINE_OK\BAD\OPEN
//m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节，不是固定的位置，是read_once里循环recv后确定的位置
//m_checked_idx指向从状态机当前正在分析的字节
http_conn::LINE_STATUS http_conn::parse_line()
{
	char temp;
	for (; m_checked_idx < m_read_idx; ++m_checked_idx)
	{
		temp = m_read_buf[m_checked_idx];
		if (temp == '\r')
		{
			if ((m_checked_idx + 1) == m_read_idx)
				return LINE_OPEN;
			else if (m_read_buf[m_checked_idx + 1] == '\n')
			{
				m_read_buf[m_checked_idx++] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			//都不符合则语法错误
			return LINE_BAD;
		}
		//如果当前字符是\n，可能上次读到\r就到末尾了
		else if (temp == '\n')
		{
			if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
			{
				m_read_buf[m_checked_idx - 1] = '\0';
				m_read_buf[m_checked_idx++] = '\0';
				return LINE_OK;
			}
			return LINE_BAD;
		}
	}
	//没有找到\r\n就继续接收
	return LINE_OPEN;
}

//主状态机
//解析http请求行，获得请求方法、url、http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
	//在HTTP报文中，请求行用来说明请求类型，要访问的资源，http版本，各个部分用\T或空格分离
	//请求行中最先含有空格和\t任一字符的位置并返回
	m_url = strpbrk(text, " \t");
	//如果没有空格或\t，报文格式有误
	if (!m_url) {
		return BAD_REQUEST;
	}
	//否则将空格处变为'\0',便于取出前面的方法
	*m_url++ = '\0';
	//取出数据，通过与GET和POST比较，确定请求方式
	char* method = text;
	if (strcasecmp(method, "GET") == 0)
		m_method = GET;
	else if (strcasecmp(method, "POST") == 0) {
		m_method = POST;
		cgi = 1;
	}
	else
		return BAD_REQUEST;

	//m_url继续向后偏移，通过查找下一个空格或\t，指向url
	//检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
	//目的是为了跳过连续的空格或\t
	m_url += strspn(m_url, " \t");
	//此时url指向/3.txt空格HTTP/1.1\r\n的起始位置
	//下面来找版本号，version会指向空格
	m_version = strpbrk(m_url, " \t");
	if (!m_version)
		return BAD_REQUEST;
	*m_version++ = '\0';
	//跳过连续空格
	m_version += strspn(m_version, " \t");

	//仅支持HTTP/1.1
	if (strcasecmp(m_version, "HTTP/1.1") != 0)
		return BAD_REQUEST;

	//单独处理带有http：//的请求报文
	// char *strchr(const char *str, int c) 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置
	//此时m_url指向/3.txt...的首部
	if (strncasecmp(m_url, "http://", 7) == 0) {
		m_url += 7;
		m_url = strchr(m_url, '/');
	}

	//增加https的情况
	if (strncasecmp(m_url, "https://", 8) == 0) {
		m_url += 8;
		m_url = strchr(m_url, '/');
	}

	//不带上述两种符号，假如url为空或者首字符不是/
	if (!m_url || m_url[0] != '/')
		return BAD_REQUEST;

	//当url为/时，显示欢迎界面(输入主机:端口，最后会默认加给/)
	if (strlen(m_url) == 1)
		strcat(m_url, "judge.html");

	//请求行处理完毕，将主状态机转移处理请求头
	m_check_state = CHECK_STATE_HEADER;
	return NO_REQUEST;
}

//解析头部
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
	//判断是空行还是请求头，解析请求行的时候一开始肯定是内容，而解析请求头完成后会遇到一个空行
	if (text[0] == '\0') {
		//空行，则判断请求头长度
		if (m_content_length != 0) {
			//说明是POST，转而解析消息体
			m_check_state = CHECK_STATE_CONTENT;
			return NO_REQUEST;
		}
		//等于0则说明是GET，那么解析到空行已经可以结束
		return GET_REQUEST;
	}
	//解析请求头的连接字段，这里只解析连接字段和请求内容长度字段
	else if (strncasecmp(text, "Connection:", 11) == 0) {
		text += 11;
		//跳过连续空格
		text += strspn(text, " \t");
		if (strcasecmp(text, "keep-alive") == 0) {
			//长连接，则linger标志设为true
			m_linger = true;
		}
	}
	//解析请求头的内容长度字段
	else if (strncasecmp(text, "Cotent-length:", 15) == 0) {
		text += 15;
		text += strspn(text, " \t");
		m_content_length = atol(text);
	}
	//解析请求头部host字段
	else if (strncasecmp(text, "Host:", 5) == 0) {
		text += 5;
		text += strspn(text, " \t");
		m_host = text;
	}
	else {
		LOG_INFO("oop!unknow header: %s", text);
		Log::get_instance()->flush();
	}
	return NO_REQUEST;
}

//解析消息内容，仅用于POST
//保存消息体，为后面的登录和注册做准备
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
	//判断buffer中是否读取了消息体，m_checked_idx是从状态机已经解析了的位置
	//注意，在最后解析消息体的时候，进入循环时没有进行整句解析，那么m_checked_idx还停留在上一次空行的位置
	//读缓冲区的字节数应该大于等于状态行、头、空行的字节数+理论上的content_length，一般是等于，但是有可能消息内容理论值少了
	//但是如果读取的数据小于了应该读的最小数据，那就出问题了
	if (m_read_idx >= (m_content_length + m_checked_idx)) {
		text[m_content_length] = '\0';
		//消息体为输入的用户名和密码
		m_string = text;
		return GET_REQUEST;
	}
	return NO_REQUEST;
}


//用于找到映射的资源
http_conn::HTTP_CODE http_conn::do_request() {
	//将初始化的m_real_file赋值为网站根目录
	strcpy(m_real_file, doc_root);
	int len = strlen(doc_root);

	//找到m_url中/的位置
	const char *p = strrchr(m_url, '/');

	//如果是post，就需要用到cgi实现登录和注册校验，2登录3注册
	if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
		//根据标志判断是登录检测还是注册检测，flag是2或者3
		char flag=m_url[1];
		char *m_url_real = (char*)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/");
		//m_url_real变成/CGISQL.cgi
		strcat(m_url_real, m_url + 2);
		//:/home/boystory/webServer/root/CGISQL.cgi
		strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
		free(m_url_real);
		
		//将用户名和密码提取出来
		//在消息体里的内容存在m_string:user=123&passwd=123，中间是&符号
		char name[100], password[100];
		int i;
		//m_string[5]为用户名第一位
		for (i = 5; m_string[i] != '&'; ++i)
			name[i - 5] = m_string[i];
		name[i - 5] = '\0';

		int j = 0;
		for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
			password[j] = m_string[i];
		password[j] = '\0';
	


	pthread_mutex_t lock;
	pthread_mutex_init(&lock, NULL);

	if (*(p + 1) == '3')
	{
		//如果是注册，先检测数据库中是否有重名的
		//没有重名的，进行增加数据
		//准备插入语句
		char *sql_insert = (char *)malloc(sizeof(char) * 200);
		strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
		strcat(sql_insert, "'");
		strcat(sql_insert, name);
		strcat(sql_insert, "', '");
		strcat(sql_insert, password);
		strcat(sql_insert, "')");

		if (users.find(name) == users.end())
		{

			pthread_mutex_lock(&lock);
			int res = mysql_query(mysql, sql_insert);
			users.insert(pair<string, string>(name, password));
			pthread_mutex_unlock(&lock);

			//表示成功,跳转登录页面
			if (!res)
				//m_url=/3/log.html
				strcpy(m_url, "/log.html");
			else
				strcpy(m_url, "/registerError.html");
		}
		else
			strcpy(m_url, "/registerError.html");
	}
	//如果是登录，直接判断
	//若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
	else if (*(p + 1) == '2')
	{
		if (users.find(name) != users.end() && users[name] == password)
			strcpy(m_url, "/welcome.html");
		else
			strcpy(m_url, "/logError.html");
		}
	}
	//如果请求资源为/0,表示跳转注册界面
	if (*(p + 1) == '0') {
		char* m_url_real = (char*)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/register.html");

		//将网站目录和/register.html进行拼接，更新到m_real_file中
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
		free(m_url_real);
	}
	//如果请求资源为/1,表示跳转至登录界面
	else if (*(p + 1) == '1') {
		char* m_url_real = (char*)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/log.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
		free(m_url_real);
	}
	//图片显示
	else if (*(p + 1) == '5')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/picture.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	//视频显示
	else if (*(p + 1) == '6')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/video.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}
	//关注页面
	else if (*(p + 1) == '7')
	{
		char *m_url_real = (char *)malloc(sizeof(char) * 200);
		strcpy(m_url_real, "/fans.html");
		strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

		free(m_url_real);
	}

	else
		//如果以上均不符合，不是登录也不是注册，则是欢迎界面，请求服务器上的一个图片
		//直接将url与网站目录拼接
		strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
	
	//通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
	//失败返回NO_RESOURCE
	if (stat(m_real_file, &m_file_stat) < 0)
		return NO_RESOURCE;

	//判断文件权限，不可读返回FORBIDDEN_REQUEST
	if (!(m_file_stat.st_mode&S_IROTH))
		return FORBIDDEN_REQUEST;
	//判断文件权限，如果是目录，返回BAD_REQUEST
	if (S_ISDIR(m_file_stat.st_mode))
		return BAD_REQUEST;

	//以只读方式获取文件描述符，通过mmap将文件映射到内存中
	//根据解析请求报文得到的url 判断请求的资源文件是否存在并且用户是否具有可读的权限。
	//若文件存在且可读，则调用mmap()将资源文件映射到内存中，m_file_address保存文件在内存中的地址。
	int fd = open(m_real_file, O_RDONLY);
	//返回可用的内存首地址
	m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	
	//避免文件描述符的浪费
	close(fd);

	//表示请求文件存在，可访问
	return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

//更新m_write_idx指针和写缓冲区中的内容
bool http_conn::add_response(const char* format, ...) {
	//如果写入内容超过写缓冲区大小
	if (m_write_idx >= WRITE_BUFFER_SIZE)
		return false;
	//定义可变参数列表
	va_list arg_list;
	//将变量arg_list初始化为传入参数
	va_start(arg_list, format);
	//将数据format从可变参数列表按照格式写入写缓冲区，返回写入数据的长度
	int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
	//如果写入的数据长度超过缓冲区剩余空间
	if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
		va_end(arg_list);
		return false;
	}
	//更新m_write_idx的位置
	m_write_idx += len;
	//清空可变参列表
	va_end(arg_list);
    LOG_INFO("request:%s", m_write_buf);
    Log::get_instance()->flush();
	return true;
}

//添加状态行
bool http_conn::add_status_line(int status, const char* title) {
	return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//添加消息报头，包括文本长度、连接状态、空行
bool http_conn::add_headers(int content_len) {
	add_content_length(content_len);
	add_linger();
	add_blank_line();
//上面三句应该要加上与操作符
}

//添加content_length，响应报文的长度
bool http_conn::add_content_length(int content_len) {
	return add_response("Content-Length:%d\r\n", content_len);
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger() {
	return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
//添加空行
bool http_conn::add_blank_line() {
	return add_response("%s", "\r\n");
}

//添加文本类型,这里是html
bool http_conn::add_content_type() {
	return add_response("Content-Type:%s\r\n", "text/html");
}

//添加文本内容
bool http_conn::add_content(const char* content) {
	return add_response("%s", content);
}
//响应报文写入缓冲区
bool http_conn::process_write(HTTP_CODE ret) {
	switch (ret) {
		//服务器端错误,500
		case INTERNAL_ERROR:
		{
			//状态行
			add_status_line(500, error_500_title);
			//消息报头
			add_headers(strlen(error_500_form));
			if (!add_content(error_500_form))
				return false;
			break;
		}
		//报文语法错误，404
		case BAD_REQUEST:
		{
			add_status_line(404, error_404_title);
			add_headers(strlen(error_404_form));
			if (!add_content(error_404_form))
				return false;
			break;
		}
		//资源没有访问权限，403
		case FORBIDDEN_REQUEST:
		{
			add_status_line(403, error_403_title);
			add_headers(strlen(error_403_form));
			if (!add_content(error_403_form))
				return false;
			break;
		}
		//文件存在，200
		case FILE_REQUEST:
		{
			add_status_line(200, ok_200_title);
			//请求资源存在
			if (m_file_stat.st_size != 0) {
				add_headers(m_file_stat.st_size);
				//第一个指针指向响应报文缓冲区，长度指向idx
				m_iv[0].iov_base = m_write_buf;
				m_iv[0].iov_len = m_write_idx;
				//第二个指针指向mmap返回的文件指针，长度指向文件大小
				m_iv[1].iov_base = m_file_address;
				m_iv[1].iov_len = m_file_stat.st_size;
				m_iv_count = 2;
				//发送的全部数据为响应报文头部信息(已经写入的长度)和文件大小
				bytes_to_send = m_write_idx + m_file_stat.st_size;
				return true;
			}
			else {
				//如果请求资源的大小为0，返回空白html文件
				const char* ok_string = "<html><body></body></heml>";
				add_headers(strlen(ok_string));
				if (!add_content(ok_string))
					return false;
			}
		}
		default:
			return false;
	}
	//除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
	m_iv[0].iov_base = m_write_buf;
	m_iv[0].iov_len = m_write_idx;
	m_iv_count = 1;
	bytes_to_send = m_write_idx;
	return true;
}

//前面把响应报文的请求行/头/空行，资源（响应内容）都用向量表示出来，现在主线程把响应报文发送给浏览器端，即资源写入缓冲区
//return false就断开连接，并移除定时器
bool http_conn::write() {
	int temp = 0;
	//如果要发送的数据长度为0
	//表示响应报文为空，一般不会出现这种情况
	if (bytes_to_send == 0) {
		//重置epollin事件
		modfd(m_epollfd, m_sockfd, EPOLLIN);
		init();
		return true;
	}
	while (1) {
		//将响应报文的状态行、消息头、空行和响应正文发送给浏览器端，从多个缓冲区一次写入文件描述符
		//发送的数据为响应信息+内容，响应信息在写缓冲区里，内容在映射的文件里，分别在两个向量里，通过writev一次写入
		//注意在writev函数里返回-1和EAGAIN表示写缓冲区满了
		//而在recv中返回-1和EAGAIN表示全部接收完了
		temp = writev(m_sockfd, m_iv, m_iv_count);
		//正常发送，temp为发送的字节数
		if (temp < 0) {
			//如果缓冲区满了
			if (errno == EAGAIN) {
				//重新注册写事件，表示还没写完，要接着写
				modfd(m_epollfd, m_sockfd, EPOLLOUT);
				return true;
			}
			//写失败
			unmap();
			return false;
		}
		//更新已发送的字节
		bytes_have_send += temp;
		bytes_to_send -= temp;
		if (bytes_have_send >= m_iv[0].iov_len) {
			//头部信息发完了,这里＋newadd是指超出m_write_idx的已发送的字节，就是属于m_iv[1]的，所以需要向后移动newadd个字节
			m_iv[0].iov_len = 0;
			//当写缓冲区发完了，m_write_idx就是写缓冲区待发送字节大小，所以括号里代表待发送的文件指向的字节
			m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
			//更新待发送字节的长度
			m_iv[1].iov_len = bytes_to_send;
		}
		else {
			m_iv[0].iov_base = m_write_buf + bytes_have_send;
			m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
		}
		//判断条件，数据已经全部发送完
		if (bytes_to_send <= 0) {
			unmap();
			//注册读事件，写完了就读
			modfd(m_epollfd, m_sockfd, EPOLLIN);

			//浏览器的请求为长连接
			if (m_linger) {
				//重新初始化HTTP对象
				init();
				return true;
			}
			else {
				return false;
			}
		}
	}
}
