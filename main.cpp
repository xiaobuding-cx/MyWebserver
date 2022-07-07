#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"
#include "log.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量
#define TIMESLOT 5

static int pipefd[2];
static sort_timer_lst timer_lst;

void addfd( int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;//对端断开链接的长可以在底层处理，不用移交到上层
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);  
}
// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );

void  sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
   // printf("发送信号到管道");
    send(pipefd[1], (char*)&msg, 1,0);
    errno = save_errno;
}

void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void addsig(int sig){
    //printf("加入信号");
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = sig_handler;
    sa.sa_flags |=SA_RESTART;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void timer_hander() {

    timer_lst.tick();
    alarm(TIMESLOT);
}

void cb_func(client_data* user_data, int epoll_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, user_data->sockfd,0);
    assert(user_data);
    close(user_data->sockfd);
}

int main( int argc, char* argv[] ) {
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    //在这里对开启日志进行初始化,默认就开启了
    int logLevel = 1;
    Log::Instance()->init(logLevel, "./log", ".log", 1024);
    LOG_INFO("=============Server init=============");
    //
    int port = atoi( argv[1] );
    //对一个对端以及关闭的socket调用两次write，第二次将会生成SIGPIPE信号（假设在收到RST之后），该信号默认结束进程
    addsig( SIGPIPE, SIG_IGN );

    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    // 端口复用
    int reuse = 1;
    //1.几遍此前将此端口作为本地端口的连接还存在，当前服务器也能绑定，一般来说是重启监听服务器时候用到、
    //如果不设置的话，会出现bind绑定错误
    //2.允许在同一个端口上启动同一个服务器的多个实例，但是要绑定一个不同的本地IP
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    //绑定和监听
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 8);

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    // 添加到epoll对象中
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    socketpair(PF_UNIX, SOCK_STREAM,0,pipefd);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0]);
    addsig(SIGALRM);
    addsig(SIGTERM);
    client_data* timeusers = new client_data[MAX_FD];
    bool timeout = false;
    bool stop_server = false;
    alarm(TIMESLOT);

    while(true) {
        //timeout 指定-1代表无期限阻塞
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        //被中断的系统调用
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {
            
            int sockfd = events[i].data.fd;
            
            if( sockfd == listenfd ) {
                
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                users[connfd].init( connfd, client_address);
                timeusers[connfd].address = client_address;
                timeusers[connfd].sockfd = connfd;
                util_timer* timer = new util_timer;
                timer->user_data = &timeusers[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3*TIMESLOT;
                timeusers[connfd].timer = timer;
                timer_lst.add_timer(timer);

            }else if((sockfd == pipefd[0])&&(events[i].events & EPOLLIN)) {
                //printf("有定时信号产生");
                int sig;
                char signals[1024];
                int re = recv(pipefd[0], signals, sizeof(signals),0);
                if(re == -1){
                    continue;
                }else if(re ==0){
                    continue;
                }else {
                    for(int j = 0; j < re; j++) {
                        switch(signals[j]) {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                            }
                        }
                    }
                }

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                util_timer * timer = timeusers[sockfd].timer;
                users[sockfd].close_conn();
                cb_func(&timeusers[sockfd],epollfd);
                    if(timer) {
                        timer_lst.del_timer(timer);
                    }
            } else if(events[i].events & EPOLLIN) {
                LOG_INFO("有读取事件产生");
                util_timer * timer = timeusers[sockfd].timer;
                if(users[sockfd].read()) {
                    pool->append(users + sockfd);
                    if(timer) {
                        time_t cur = time(NULL);
                        timer->expire =  cur + 3*TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    users[sockfd].close_conn();
                    cb_func(&timeusers[sockfd],epollfd);
                    if(timer) {
                        timer_lst.del_timer(timer);
                    }
                }

            }  else if( events[i].events & EPOLLOUT ) {

                if( !users[sockfd].write() ) {
                    util_timer* timer = timeusers[sockfd].timer;
                    users[sockfd].close_conn();
                     cb_func(&timeusers[sockfd],epollfd);
                     if(timer) {
                        timer_lst.del_timer(timer);
                    }
                }

            }

            if(timeout) {
               //printf("定时删除");
                timer_hander();
                timeout = false;
            }
        }
    }
    
    close( epollfd );
    close( listenfd );
    close(pipefd[0]);
    close(pipefd[1]);
    delete [] timeusers;
    delete [] users;
    delete pool;
    return 0;
}