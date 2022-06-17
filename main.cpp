#include<iostream>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include "locker.h"
#include  "threadpool.h"
#include<signal.h>
#include "http_conn.h"


#define MAX_FD  65535
#define MAX_EVENT_NUMBER 10000  //一次监听最大数量
//添加信号捕捉
void  addsig(int sig, void (handler)(int)) {
    struct   sigaction sa;
    memset(&sa, '\0',sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa,NULL);
}

//添加文件描述符到epoll中
extern void addfd(int epollfd,  int fd, bool  one_shot);
//从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
//修改文件描述符
extern void modfd(int epollfd, int fd, int ev);
int  main(int argc, char* argv[])
{
    //至少传入一个端口号
    if(argc<=1) {
        printf("按照如下格式运行：%s port number\n",basename(argv[0]));
        exit(-1);
    }

    int  port=atoi(argv[1]);


    //对signal信号处理
    addsig(SIGPIPE, SIG_IGN);

    //初始化线程池子
    threadpool<http_conn> * pool = NULL;

    try{
        pool =  new  threadpool<http_conn>;
    }catch(...) {
        exit(-1);
    }

    //创建一个数组保存所有客户端信息
    http_conn * users  =    new  http_conn[MAX_FD];

    int  listenfd = socket(PF_INET,SOCK_STREAM,0);//创建监听的文件描述符  字节流传输  0  默认TCP 

    // 设置端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family= PF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port =  htons(port); //传入参数获取到了Port

    bind(listenfd ,  (struct sockaddr*)&address, sizeof(address));

    //监听
    listen(listenfd,5);

    //创建epoll 对象，事件数组,添加监听的文件描述符
    epoll_event   events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    //将监听的文件描述符 添加到epoll对象中
    addfd(epollfd, listenfd, false);

    http_conn::m_epollfd = epollfd;

    while(true) {
        int  num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);//检测到了几个事件
        if (num < 0  && errno != EINTR) {
            printf("epoll_wait失败了");
            break;//失败
        }
        
        //循环遍历事件数组
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd) {
                //有客户点链接进来
                struct  sockaddr_in  client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address,&client_addrlen );
                printf("有客户端连接");
                if(http_conn :: m_user_count >= MAX_FD) {
                    //连接数满了
                    //你可以选择给服务器发回一些消息： 比如说 服务器繁忙
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化，放到数组当中
                users[connfd].init(connfd, client_address);
            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) ) {
                //对方异常断开或者错误时间
                users[sockfd].close_conn();
            }else if(events[i].events  & EPOLLIN) {
                 //printf("有数据需要读取");
                 if(users[sockfd].read()) {
                     //一次性把数据读完
                     pool ->append(users + sockfd);
                 }else {
                     users[sockfd].close_conn();
                 }
            }else if(events[i].events & EPOLLOUT) {
               // printf("有数据需要写入");
                if(! users[sockfd].write()) //一次性写完所有数据
                {
                    users[sockfd].close_conn();
                }
            }
        }
    }

        close(epollfd);
        close(listenfd);
        delete [] users;
        delete pool;

    return 0;
}