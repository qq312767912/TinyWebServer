# TinyWebServer
实现了一个轻量级web服务器，支持GET和POST方法

使用线程池、epoll（ET和LT均实现）和模拟Proactor模式的并发模型 
利用状态机解析HTTP请求报文，支持GET和POST请求
通过访问服务器数据库实现Web端注册、登录功能，可以请求服务器图片和视频文件
利用单例模式实现同步/异步日志系统，记录服务器运行状态 
经webbench压力测试可以实现上万的并发连接数据交换  

感谢
两猿社的学习资料和帮助
《Linux高性能服务器编程》 游双著