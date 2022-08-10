server: main.cpp threadpool.h block_queue.h http_conn.h locker.h http_conn.cpp log.cpp log.h sql_connection_pool.cpp sql_connection_pool.h
	g++ -o server main.cpp threadpool.h http_conn.h locker.h http_conn.cpp log.cpp log.h sql_connection_pool.cpp sql_connection_pool.h -lpthread -lmysqlclient

CGISQL.cgi:sign.cpp sql_connection_pool.cpp sql_connection_pool.h
	g++ -o ./root/CGISQL.cgi sign.cpp sql_connection_pool.cpp sql_connection_pool.h

clean: 
	rm -r server
	rm -r ./root/CGISQL.cgi
