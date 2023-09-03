#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int main(int argc,char * argv[])
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

    int sock = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sock >= 0 );

    int ret = bind( sock, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( sock, 5 );
    assert( ret != -1 );

    struct sockaddr_in client;
    socklen_t client_addrlength = sizeof( client );
    int connfd = accept( sock, ( struct sockaddr* )&client, &client_addrlength );
    if ( connfd < 0 )
    {
        printf( "errno is: %d\n", errno );
    }
    else
    {
        /* 首先关闭文件描述符 STDOUT_FILENO（其值是1） */
        close( STDOUT_FILENO );
        /* 然后复制 socket 文件描述符 connfd。由于 dup 总是返回系统中最小的可用文件描述符，所以它的返回值实际上是 1，即使之前关闭的标准输出文件描述符的值。 */
        dup( connfd );
        /* 这样一来服务器输出到标准输出的内容就会直接被发送到与客户连接对应的 socket 上，因此 printf 调用的输出将被客户端获得。 */
        printf( "abcd\n" );
        close( connfd );
    }

    close( sock );
    return 0;
}