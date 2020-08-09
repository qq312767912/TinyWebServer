# TinyWebServer
实现了一个轻量级web服务器，支持GET和POST方法

使用线程池、epoll（ET和LT均实现）和模拟Proactor模式的并发模型\<br>  
利用状态机解析HTTP请求报文，支持GET和POST请求\<br>  
通过访问服务器数据库实现Web端注册、登录功能，可以请求服务器图片和视频文件\<br>  
利用单例模式实现同步/异步日志系统，记录服务器运行状态\<br>  
经webbench压力测试可以实现上万的并发连接数据交换\<br>  

感谢
两猿社的学习资料和帮助\<br>  
《Linux高性能服务器编程》 游双著
