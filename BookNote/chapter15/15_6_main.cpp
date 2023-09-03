#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "chapter14/14_2_locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd( int epollfd, int fd, bool one_shot );
extern int removefd( int epollfd, int fd );

/* 信号处理函数 */
void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    // 指定信号处理函数
    sa.sa_handler = handler;
    if( restart )
    {
        // 将程序收到信号时的行为设置为 "重新调用被该信号终止的系统调用"
        sa.sa_flags |= SA_RESTART;
    }
    // 将信号集中所有信号设置为 sa 的信号掩码
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

/* 显示错误 */
void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    // 向 connfd 上发送错误信息 info
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
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

    // 忽略 SIGPIPE 信号
    addsig( SIGPIPE, SIG_IGN );

    // 创建线程池
    threadpool< http_conn >* pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    // 预先为每个可能的客户连接分配一个 http_conn 对象
    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;

    // 创建 socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    // 初始化 socket 地址信息
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 绑定 socket 地址
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    // 监听 socket
    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );    // 创建事件表
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );  // 向 epollfd 上注册事件
    http_conn::m_epollfd = epollfd;

    while( true )
    {
        // 获得等待事件数
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            // 如果就绪的文件描述符是 listenfd，则处理新的连接
            if( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                // 从等待队列中选择一个客户端的 socket，用该 socket 来与被接受连接的客户端进行通信
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                // 接收客户端连接失败
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                // 客户数量太多
                if( http_conn::m_user_count >= MAX_FD )
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }
                // 初始化客户连接
                users[connfd].init( connfd, client_address );
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                // 如果有异常，直接关闭客户连接
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN )
            {
                // 根据读的结果，决定是将任务添加到线程池，还是关闭连接
                if( users[sockfd].read() )
                {
                    // 添加到线程池中
                    pool->append( users + sockfd );
                }
                else
                {
                    // 关闭客户连接
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )
            {
                // 根据写的结果，决定是否关闭连接
                if( !users[sockfd].write() )
                {
                    // 关闭连接
                    users[sockfd].close_conn();
                }
            }
            else
            {}
        }
    }

    close( epollfd );   // 关闭事件表
    close( listenfd );  // 关闭 socket 连接
    delete [] users;    // 释放用户表资源
    delete pool;        // 释放线程池资源
    return 0;
}