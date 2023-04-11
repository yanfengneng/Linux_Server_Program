#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

/* 定义两种 HTTP 状态码和状态信息 */
static const char* status_line[2]={
    "200 OK",
    "500 Internal server error"
};

int main(int argc,char* argv[])
{
    if(argc <= 3)
    {
        printf("usage: %s ip_address port_number filename\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);
    /* 将目标文件作为程序的第三个参数传入 */
    const char* file_name = argv[3];
    struct sockaddr_in server_address;
    bzero( &server_address, sizeof( server_address ) );
    server_address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &server_address.sin_addr );
    server_address.sin_port = htons( port );

    int sock = socket( PF_INET, SOCK_STREAM, 0 );
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
         char header_buf[ BUFFER_SIZE ];
        memset( header_buf, '\0', BUFFER_SIZE );
        char* file_buf;
        struct stat file_stat;
        bool valid = true;
        int len = 0;
        if( stat( file_name, &file_stat ) < 0 )
        {
            valid = false;
        }
        else
        {
            if( S_ISDIR( file_stat.st_mode ) )
            {
                valid = false;
            }
            else if( file_stat.st_mode & S_IROTH )
            {
                int fd = open( file_name, O_RDONLY );
                file_buf = new char [ file_stat.st_size + 1 ];
                memset( file_buf, '\0', file_stat.st_size + 1 );
                if ( read( fd, file_buf, file_stat.st_size ) < 0 )
                {
                    valid = false;
                }
            }
            else
            {
                valid = false;
            }
        }
        
        if( valid )
        {
            ret = snprintf( header_buf, BUFFER_SIZE-1, "%s %s\r\n", "HTTP/1.1", status_line[0] );
            len += ret;
            ret = snprintf( header_buf + len, BUFFER_SIZE-1-len, 
                             "Content-Length: %d\r\n", file_stat.st_size );
            len += ret;
            ret = snprintf( header_buf + len, BUFFER_SIZE-1-len, "%s", "\r\n" );
            struct iovec iv[2];
            iv[ 0 ].iov_base = header_buf;
            iv[ 0 ].iov_len = strlen( header_buf );
            iv[ 1 ].iov_base = file_buf;
            iv[ 1 ].iov_len = file_stat.st_size;
            ret = writev( connfd, iv, 2 );
        }
        else
        {
            ret = snprintf( header_buf, BUFFER_SIZE-1, "%s %s\r\n", "HTTP/1.1", status_line[1] );
            len += ret;
            ret = snprintf( header_buf + len, BUFFER_SIZE-1-len, "%s", "\r\n" );
            send( connfd, header_buf, strlen( header_buf ), 0 );
        }
        close( connfd );
        delete [] file_buf;
    }
    close(sock);
    return 0;
}