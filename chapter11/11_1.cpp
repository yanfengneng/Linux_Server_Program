#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

/* 超时连接函数 */
int timeout_connect( const char* ip, int port, int time )
{
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 创建 socket
    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sockfd >= 0 );

    /* 通过选项 SO_RCVTIMEO 和 SO_SNDTIMEO 所设置的超时时间类型是 timeval，这和 select 系统调用的超时参数类型相同。 */
    struct timeval timeout;
    timeout.tv_sec = time;  // 秒数
    timeout.tv_usec = 0;    // 毫秒数
    socklen_t len = sizeof( timeout );
    // 在 sockfd 上设置发送数据超时选项
    ret = setsockopt( sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, len );
    assert( ret != -1 );

    /* 客户端开始监听 socket：指定监听服务器的 IP 地址，然后指定服务器的长度。connet失败就会返回 -1，这样程序就会退出了。 */
    ret = connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) );
    if ( ret == -1 )
    {
        /* 超时对应的错误号是 EINPROGRESS。若 if 条件成立，则接下来就能处理定时任务了。 */
        if( errno == EINPROGRESS )
        {
            printf( "connecting timeout\n" );
            return -1;
        }
        printf( "error occur when connecting to server\n" );
        return -1;
    }

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

    // 调用超时连接函数
    int sockfd = timeout_connect( ip, port, 10 );
    if ( sockfd < 0 )
    {
        return 1;
    }
    return 0;
}