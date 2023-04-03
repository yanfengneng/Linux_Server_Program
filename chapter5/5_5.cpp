#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

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

    /*创建 socket：使用 IPv4 协议；使用流服务 SOCK_STREAM（表示传输层使用 TCP 协议）；使用默认协议*/
    int sock = socket( PF_INET, SOCK_STREAM, 0 );
    /*socket 创建失败就发生断言*/
    assert( sock >= 0 );

    /*  
        命名 socket：将 address 所指的 socket 地址分配给为命名的 sock 文件描述符，使用sizeof 来指出该地址的长度
        bind 成功返回 0，失败返回 -1 并设置 errno。
    */
    int ret = bind( sock, ( struct sockaddr* )&address, sizeof( address ) );
    // 失败就直接断言退出程序了
    assert( ret != -1 );

    /*监听 socket：监听 sock，指定内核监听队列的最大长度为 5。*/
    ret = listen( sock, 5 );
    // 失败就直接断言退出程序了
    assert( ret != -1 );

    /*暂停 20s 以等待客户端连接和相关操作（掉线或者退出）完成*/
    sleep(20);

    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof( client );
    /*接受 socket：sock 表示之前执行过 listen 系统调用的监听 socket；然后获取客户端的 socket 地址，并指定客户端地址的长度。*/
    int connfd = accept( sock, ( struct sockaddr* )&client, &client_addrlength );
    // 接受连接失败，打印错误
    if ( connfd < 0 )
    {
        printf( "errno is: %d\n", errno );
    }
    else
    {
        /*接受连接成功则打印出客户端的 IP 地址和端口号*/
        char remote[INET_ADDRSTRLEN ];
        printf( "connected with ip: %s and port: %d\n", 
            inet_ntop( AF_INET, &client.sin_addr, remote, INET_ADDRSTRLEN ), ntohs( client.sin_port ) );
        close( connfd );
    }

    close( sock );
    return 0;
}