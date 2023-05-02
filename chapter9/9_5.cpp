#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 1023

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

// 超时连接函数，参数分别是服务器 IP 地址、端口号和超时时间（毫秒）。函数成功时返回已经处于连接状态的 socket，失败则返回 -1。
int unblock_connect( const char* ip, int port, int time )
{
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    // 将 sockfd 设置为非阻塞的
    int fdopt = setnonblocking( sockfd );
    /* 开始监听服务器的 socket 地址。connet 成功时返回0，一旦成功建立连接，sockfd 就唯一地标识了这个连接，客户端就可以通过读写 sockfd 来与服务器进行通信了。
    connect 失败则返回 -1 并设置 errno */
    ret = connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) );
    if ( ret == 0 )
    {
        // 如果连接成功，则回复 sockfd 的属性，并立即返回之
        printf( "connect with server immediately\n" );
        // 还原 sockfd 的事件标志
        fcntl( sockfd, F_SETFL, fdopt );
        return sockfd;
    }
    else if ( errno != EINPROGRESS )
    {
        // 如果连接没有立即建立，那么只有当 errno 是 EINPROGRESS 时才表示连接还在进行，否则出错返回
        printf( "unblock connect not support\n" );
        return -1;
    }

    fd_set readfds;     // 读事件
    fd_set writefds;    // 写事件
    struct timeval timeout;

    FD_ZERO( &readfds );
    // 给 sockfd 设置写事件
    FD_SET( sockfd, &writefds );

    timeout.tv_sec = time;  // 秒数
    timeout.tv_usec = 0;    // 微秒数

    // 开始监听写事件
    ret = select( sockfd + 1, NULL, &writefds, NULL, &timeout );
    if ( ret <= 0 )
    {
        // select 超时或者出错，立即返回
        printf( "connection time out\n" );
        close( sockfd );
        return -1;
    }

    // 写事件未发生
    if ( ! FD_ISSET( sockfd, &writefds  ) )
    {
        printf( "no events on sockfd found\n" );
        close( sockfd );
        return -1;
    }

    int error = 0;
    socklen_t length = sizeof( error );
    // 调用 getsockopt 来获取并清除 sockfd 上的错误
    if( getsockopt( sockfd, SOL_SOCKET, SO_ERROR, &error, &length ) < 0 )
    {
        printf( "get socket option failed\n" );
        close( sockfd );
        return -1;
    }

    // 错误号不为 0 表示连接出错
    if( error != 0 )
    {
        printf( "connection failed after select with the error: %d \n", error );
        close( sockfd );
        return -1;
    }
    // 连接成功
    printf( "connection ready after select with the socket: %d \n", sockfd );
    fcntl( sockfd, F_SETFL, fdopt );
    return sockfd;
}

// 客户端程序
int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    int sockfd = unblock_connect( ip, port, 10 );
    if ( sockfd < 0 )
    {
        return 1;
    }
    // 关闭 sockfd 上的写操作
    shutdown( sockfd, SHUT_WR );
    sleep( 200 );
    printf( "send data out\n" );
    send( sockfd, "abc", 3, 0 );
    //sleep( 600 );
    return 0;
}