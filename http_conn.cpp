#include "http_conn.h"

//对静态变量初始化
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

//设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//向epoll 中添加需要监听的文件描述符
void addfd(int epollfd,  int fd, bool  one_shot) {
    epoll_event  event;
    event.data.fd = fd;
    event.events = EPOLLIN |  EPOLLRDHUP; //异常断开后，底层处理，不用交给上层  ，在这里设置水平触发还是边缘触发

    if(one_shot) {
        event.events |= EPOLLONESHOT;//注册 oneshot事件
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);//边缘触发的话，一次性读完，设置非阻塞，不然读完了数据会阻塞
}

//向epoll中移除监听的文件描述符

void   removefd(int epollfd,  int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符
//重置socket 上的oneshot事件，确保下一次可读时，EPOLLIN事件触发
void  modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev  | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD,  fd,  &event);
}

void  http_conn::init (int sockfd, const sockaddr_in  &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //添加到epoll中
    addfd(m_epollfd, sockfd, true);
    m_user_count ++; //用户数+1

}

void http_conn::close_conn() {
    //关闭链接
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::read() {
    printf("一次性读完数据\n");
    return true;
}

bool http_conn::write() {
    printf("一次性写完数据\n");
    return true;
}

void http_conn::process() {
    //线程池中的工作线程调用，处理http请求的入口函数
    //解析http 请求

    printf("parse request, creat response\n");
    //生成响应
}