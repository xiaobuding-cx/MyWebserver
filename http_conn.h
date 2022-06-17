#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include "locker.h"
#include <sys/uio.h>

class  http_conn {
    public:
    static int   m_epollfd;  //所有socket上的事件都被注册到同一个对象中
    static int   m_user_count;//统计所有用户数量

    
    http_conn(){}
    ~http_conn(){}

    void  process();  //处理客户端的请求
    void  init (int sockfd, const sockaddr_in  &addr);//初始化客户端信息
    void close_conn();
    bool  read();//非阻塞读
    bool write();//非阻塞写
    
    private:
            int  m_sockfd; //该HTTP链接的socket
            sockaddr_in  m_address; //通信的socket地址
};














#endif