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
#include <functional>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024

struct fds
{
   int epollfd;
   int sockfd;
};

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

// 将 fd 上的 EPOLLIN 和 EPOLLET 事件注册到 epollfd 指示的 epoll 内核事件表中，参数 oneshot 指定是否注册 fd 上的 EPOLLONESHOT 事件
void addfd( int epollfd, int fd, bool oneshot )
{
    epoll_event event;
    event.data.fd = fd;
    // 将事件设置为可读和 ET 模式
    event.events = EPOLLIN | EPOLLET;
    // 给 fd 上注册 EPOLLONESHOT 事件
    // 对于注册了 EPOLLONESHOT 事件的文件描述符，OS 最多触发其上注册的一个可读、可写或者异常事件，且触发一次，除非使用 epoll_ctl 函数重置该文件描述符上注册的 EPOLLONESHOT 事件
    if( oneshot )
    {
        event.events |= EPOLLONESHOT;
    }
    // 往事件表中注册 fd 上的事件
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    // 将文件描述符设置为非阻塞的
    setnonblocking( fd );
}

// 重置 fd 上的事件。这样操作之后，尽管 fd 上的 EPOLLONESHOT 事件被注册，但是操作系统依然被触发 fd 上的 EPOLLIN 事件，且只触发一次
void reset_oneshot( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    // 事件设置为可读、ET 模式、EPOLLONESHOT 事件
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    // 修改 fd 上的注册事件
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

// 工作线程
void* worker( void* arg )
{
    int sockfd = ( (fds*)arg )->sockfd;
    int epollfd = ( (fds*)arg )->epollfd;
    printf( "start new thread to receive data on fd: %d\n", sockfd );
    char buf[ BUFFER_SIZE ];
    memset( buf, '\0', BUFFER_SIZE );
    // 循环等待 sockfd 上的数据，直到遇到 EAGAIN 错误
    while(1)
    {
        // recv 调用成功时返回实际读取到的数据长度
        int ret = recv( sockfd, buf, BUFFER_SIZE-1, 0 );
        // 读数据结束
        if( ret == 0 )
        {
            close( sockfd );
            printf( "foreiner closed the connection\n" );
            break;
        }
        // 读数据失败
        else if( ret < 0 )
        {
            // 错误为再次尝试，就重置 sockfd 上的事件
            if( errno == EAGAIN )
            {
                reset_oneshot( epollfd, sockfd );
                printf( "read later\n" );
                break;
            }
        }
        else
        {
            // 打印读取到的数据
            printf( "get content: %s\n", buf );
            // 休眠 5s，模拟数据处理过程
            sleep( 5 );
        }
    }
    printf( "end thread receiving data on fd: %d\n", sockfd );
}

// 服务端程序
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

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    // 绑定客户端的 socket 地址
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    // 监听客户端请求
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    // 创建事件表
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    // 监听 socket，listenfd 上是不能注册 EPOLLONESHOT 事件的，否则应用程序只能处理一个客户连接。因为后续的客户连接请求将不再触发 listenfd 上的 EPOLLIN 事件
    addfd( epollfd, listenfd, false );

    while( 1 )
    {
        // 在一段超时时间内等待一组文件描述符上的事件。若检测到事件，则将 epollfd 上的事件从从内核事件表中复制到 events 中
        // 该函数成功则返回就绪文件描述符的个数；失败时就返回 -1 并设置 errno。
        int ret = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ret < 0 )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < ret; i++ )
        {
            int sockfd = events[i].data.fd;
            if ( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                // 接受客户端连接
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                // 对每个非监听文件描述符都注册 EPOLLONESHOT 事件
                addfd( epollfd, connfd, true );
            }
            else if ( events[i].events & EPOLLIN )
            {
                // 声明线程
                pthread_t thread;
                fds fds_for_new_worker;
                fds_for_new_worker.epollfd = epollfd;
                fds_for_new_worker.sockfd = sockfd;
                // 新启动一个工作线程为 sockfd 服务
                pthread_create( &thread, NULL, worker, ( void* )&fds_for_new_worker );
            }
            else
            {
                printf( "something else happened \n" );
            }
        }
    }
    // 关闭 socket
    close( listenfd );
    return 0;
}