#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>

#define BUFFER_SIZE 64

// 客户端
/* 客户端程序使用 poll 同时监听用户输入和网络连接，并利用 splice 函数将用户输入内容直接定向到网络连接上以发送之，从而实现数据的零拷贝 */
int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    // 服务器的 socket 地址：ip地址、端口号。在启动进程时，都要使用服务器的 ip 地址和端口号。
    struct sockaddr_in server_address;
    bzero( &server_address, sizeof( server_address ) );
    server_address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &server_address.sin_addr );
    server_address.sin_port = htons( port );

    // 创建 socket
    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( sockfd >= 0 );
    // 客户端主动连接服务端 socket 地址：一旦连接成功，sockfd 就成功标识服务端的 socket 地址了，就能通过这个地址来与服务器进行通信了，也就是读写 sockfd 来与服务器进行通信
    if ( connect( sockfd, ( struct sockaddr* )&server_address, sizeof( server_address ) ) < 0 )
    {
        printf( "connection failed\n" );
        close( sockfd );
        return 1;
    }

    pollfd fds[2];
    // 注册文件描述符 0（标准输入）和文件描述符 sockfd 上的可读事件
    fds[0].fd = 0;
    fds[0].events = POLLIN;                 // 事件注册为可读
    fds[0].revents = 0;                     // 实际发生的事件，由内核填充
    fds[1].fd = sockfd;                     // 读写 sockfd 来与服务器进行通信
    fds[1].events = POLLIN | POLLRDHUP;     // 事件注册为可读和 TCP 连接
    fds[1].revents = 0;                     // 实际发生的事件，由内核填充
    char read_buf[BUFFER_SIZE];
    int pipefd[2];
    // 创建管道，用来实现进程间通信：fd[0] 只能用于从管道读出数据，fd[1] 只能用于往管道写入数据，而不能反过来使用
    int ret = pipe( pipefd );
    assert( ret != -1 );

    while( 1 )
    {
        // 开始监听 fds 上的事件
        ret = poll( fds, 2, -1 );
        if( ret < 0 )
        {
            printf( "poll failure\n" );
            break;
        }

        // 服务端：实际事件已经发生且 TCP 连接被对方关闭
        if( fds[1].revents & POLLRDHUP )
        {
            printf( "server close the connection\n" );
            break;
        }

        // 服务端：实际事件已经发生且数据是可读的
        else if( fds[1].revents & POLLIN )
        {
            memset( read_buf, '\0', BUFFER_SIZE );
            // 开始接收服务器发来的数据
            recv( fds[1].fd, read_buf, BUFFER_SIZE-1, 0 );
            //printf( "The server's data is %s\n", read_buf );
        }

        // 客户端：实际事件已经发生且数据是可读的
        if( fds[0].revents & POLLIN )
        {
            // 使用 splice 将用户输入的数据直接写到 sockfd 上（零拷贝）
            // 若 off_in 被设置为 NULL，则标识从输入数据流的当前偏移位置读入；若 off_in 不为 NULL，则它将指出具体的偏移位置。同理 off_out 亦是如此。
            // 往 pipfd[1] 写入数据，也就是向服务器写入数据；从 pipfd[0] 读取数据，也就是从本地客户端读取数据。
            ret = splice( 0, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE );
            ret = splice( pipefd[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE );
        }
    }
    // 关闭对服务器进行读写的文件描述符 sockfd
    close( sockfd );
    return 0;
}