#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define BUF_SIZE 1024

/* 接收带外数据 */
int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    /* 获得 ip 地址 */
    const char* ip = argv[1];
    /* 获得端口号 */
    int port = atoi( argv[2] );

    struct sockaddr_in address;
    // 将 ip 地址初始化为 0
    bzero( &address, sizeof( address ) );
    // 地址族设置为 IPv4 地址
    address.sin_family = AF_INET;
    /* 将参数 ip 的字符串IP地址转换为用大端方式整数表示的IP地址存放在 address.sin_addr 指向的内存中，并使用 IPv4 的地址族 */
    inet_pton( AF_INET, ip, &address.sin_addr );
    /* 将小端方式的端口号转换为大端方式的端口号，然后赋值给本地的端口号 */
    address.sin_port = htons( port );

    /* 创建 socket：使用 UNIX 本地域协议族；传输层使用 TCP 协议；最后一个参数使用默认协议 */
    int sock = socket( PF_INET, SOCK_STREAM, 0 );
    /* 创建 socket 失败就发生断言 */
    assert( sock >= 0 );

    /* 命名 socket：将本地 address 所指的 socket 地址分配给未命名的 socket 文件描述符，然后指定本地 address 地址的长度 */
    int ret = bind( sock, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    /* 监听 sock 指定的地址，并规定监听队列的最大长度为 5 */
    ret = listen( sock, 5 );
    assert( ret != -1 );

    /* 声明客户端的结构体 */
    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof( client );
    /*  从监听队列中接受一个连接，然后客户端来获取被接受连接的远程 socket 地址
        监听成功的话，accept返回一个 socket，可以使用该 socket 进行通信。 */
    int connfd = accept( sock, ( struct sockaddr* )&client, &client_addrlength );
    /* 监听失败 */
    if ( connfd < 0 )
    {
        printf( "errno is: %d\n", errno );
    }
    /* 监听成功 */
    else
    {
        char buffer[ BUF_SIZE ];

        /* 初始化空字符串 */
        memset( buffer, '\0', BUF_SIZE );
        /* 开始读取监听 socket 上的数据，然后将数组存放在字符数组 buffer 的起止位置处，recv成功的话就会返回接受到的字节数 */
        ret = recv( connfd, buffer, BUF_SIZE-1, 0 );
        printf( "got %d bytes of normal data '%s'\n", ret, buffer );

        /* 初始化空字符串 */
        memset( buffer, '\0', BUF_SIZE );
        /* 开始读取监听 socket 上的数据，然后将数组存放在字符数组 buffer 的起止位置处，recv成功的话就会返回接受到的字节数 */
        ret = recv( connfd, buffer, BUF_SIZE-1, MSG_OOB );
        printf( "got %d bytes of oob data '%s'\n", ret, buffer );

        /* 初始化空字符串 */
        memset( buffer, '\0', BUF_SIZE );
        /* 开始读取监听 socket 上的数据，然后将数组存放在字符数组 buffer 的起止位置处，recv成功的话就会返回接受到的字节数 */
        ret = recv( connfd, buffer, BUF_SIZE-1, 0 );
        printf( "got %d bytes of normal data '%s'\n", ret, buffer );

        /* 关闭远程连接的 socket */
        close( connfd );
    }

    /* 关闭本地 socket */ 
    close( sock );
    return 0;
}