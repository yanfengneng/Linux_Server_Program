#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512
#define UDP_BUFFER_SIZE 1024

/* 将文件描述符设置成非阻塞的 */
int setnonblocking( int fd )
{
    // 6.8 fcntl 函数
    /* 获取 fd 的状态标志，这些标志包括由 open 系统调用设置的标志和访问模式 */
    int old_option = fcntl( fd, F_GETFL );
    // 加上无锁标志
    int new_option = old_option | O_NONBLOCK;
    // 给 fd 设置新标志
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

// 将文件描述符 fd 上的 EPOLLIN 注册到 epollfd 指示的 epoll 内核事件表中
void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    //event.events = EPOLLIN | EPOLLET;
    event.events = EPOLLIN;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 创建 TCP socket，并将其绑定到端口 port 上。SOCK_STREAM 表示流服务，也就是表示传输层使用 TCP 协议
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    // 该端口绑定 TCP
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    // 开始启动监听
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    // 创建 UDP socket，并将其绑定到 port 上
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );
    // SOCK_DGRAM 表示数据报服务，也就是表示传输层使用 UDP 协议。
    int udpfd = socket( PF_INET, SOCK_DGRAM, 0 );
    assert( udpfd >= 0 );

    // 该端口绑定 UDP
    ret = bind( udpfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    // 注册 TCP socket 和 UDP socket 上的可读事件
    addfd( epollfd, listenfd );
    addfd( epollfd, udpfd );

    while( 1 )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( number < 0 )
        {
            printf( "epoll failure\n" );
            break;
        }
    
        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            // 监听到 TCP 请求，然后从请求队列中选择一个 socket 地址作为客户端 socket 地址，之后对该 socket 地址进行读写来向客户端传输数据
            if ( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                // 将 TCP 文件描述上可读时间注册到 epollfd 中
                addfd( epollfd, connfd );
            }
            else if ( sockfd == udpfd )
            {
                char buf[ UDP_BUFFER_SIZE ];
                memset( buf, '\0', UDP_BUFFER_SIZE );
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );

                // 接收来自客户端的 UDP 数据
                ret = recvfrom( udpfd, buf, UDP_BUFFER_SIZE-1, 0, ( struct sockaddr* )&client_address, &client_addrlength );
                if( ret > 0 )
                {
                    // 向客户端发送数据
                    sendto( udpfd, buf, UDP_BUFFER_SIZE-1, 0, ( struct sockaddr* )&client_address, client_addrlength );
                }
            }
            else if ( events[i].events & EPOLLIN )
            {
                char buf[ TCP_BUFFER_SIZE ];
                while( 1 )
                {
                    memset( buf, '\0', TCP_BUFFER_SIZE );
                    // 接收客户端的 TCP 数据
                    ret = recv( sockfd, buf, TCP_BUFFER_SIZE-1, 0 );
                    if( ret < 0 )
                    {
                        if( ( errno == EAGAIN ) || ( errno == EWOULDBLOCK ) )
                        {
                            break;
                        }
                        close( sockfd );
                        break;
                    }
                    else if( ret == 0 )
                    {
                        close( sockfd );
                    }
                    // 向客户端发送 TCP 数据
                    else
                    {
                        send( sockfd, buf, ret, 0 );
                    }
                }
            }
            else
            {
                printf( "something else happened \n" );
            }
        }
    }

    close( listenfd );
    return 0;
}