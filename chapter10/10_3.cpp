#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#define BUF_SIZE 1024

static int connfd;

/* SIGURG 信号的处理函数 */
void sig_urg( int sig )
{
    int save_errno = errno;
    char buffer[ BUF_SIZE ];
    memset( buffer, '\0', BUF_SIZE );
    // 接收数据
    int ret = recv( connfd, buffer, BUF_SIZE-1, MSG_OOB );
    printf( "got %d bytes of oob data '%s'\n", ret, buffer );
    errno = save_errno;
}

/* 设置信号的处理函数 */
void addsig( int sig, void ( *sig_handler )( int ) )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    // 指定信号处理函数
    sa.sa_handler = sig_handler;
    // 将程序收到信号时的行为设置为 "重新调用被该信号终止的系统调用"
    sa.sa_flags |= SA_RESTART;
    // 将信号集中所有信号设置为 sa 的信号掩码
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

// 服务器程序
int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 创建 socket
    int sock = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sock >= 0 );

    // 绑定 socket
    int ret = bind( sock, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    // 监听 socket
    ret = listen( sock, 5 );
    assert( ret != -1 );

    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof( client );
    // 从等待队列中选择一个客户端的 socket，用该 socket 来与被接受连接的客户端进行通信
    connfd = accept( sock, ( struct sockaddr* )&client, &client_addrlength );
    if ( connfd < 0 )
    {
        printf( "errno is: %d\n", errno );
    }
    else
    {
        // 设置信号处理函数
        addsig( SIGURG, sig_urg );
        /* 在使用 SIGURG 信号之前，我们必须设置 socket 的宿主进程或进程组 */
        fcntl( connfd, F_SETOWN, getpid() );

        char buffer[ BUF_SIZE ];
        /* 循环接收普通数据 */
        while( 1 )
        {
            memset( buffer, '\0', BUF_SIZE );
            // 接收数据
            ret = recv( connfd, buffer, BUF_SIZE-1, 0 );
            if( ret <= 0 )
            {
                break;
            }
            printf( "got %d bytes of normal data '%s'\n", ret, buffer );
        }

        close( connfd );
    }

    close( sock );
    return 0;
}