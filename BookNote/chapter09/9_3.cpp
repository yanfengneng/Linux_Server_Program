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

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10

/* 将文件描述符设置成非阻塞的 */
int setnonblocking( int fd )
{
    // 通过 fcntl 来获取的 fd 的状态标志，包括 open 系统调用设置的标志和访问模式
    int old_option = fcntl( fd, F_GETFL );
    // 设置为非阻塞的
    int new_option = old_option | O_NONBLOCK;
    // 将 fd 的标志设置为 新操作的标志，也就是设置为非阻塞的
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

/* 将文件描述符 fd 上的 EPOLLIN 注册到 epollfd 指示的 epoll 内核事件表中，参数 enable_et 指定是否对 fd 启用 ET 模式 */
void addfd( int epollfd, int fd, bool enable_et )
{
    epoll_event event;
    event.data.fd = fd;
    // 事件指定为 数据可读
    event.events = EPOLLIN;
    // 判断是否启用 ET 模式
    if( enable_et )
    {
        event.events |= EPOLLET;
    }
    // 将文件描述符 fd 上的 EPOLLIN（数据可读） 注册到 epollfd 指示的 epoll 内核事件表中，参数 enable_et 指定是否对 fd 启用 ET 模式
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

/* LT 模式的工作流程 */
void lt( epoll_event* events, int number, int epollfd, int listenfd )
{
    // 缓冲字符数组
    char buf[ BUFFER_SIZE ];
    for ( int i = 0; i < number; i++ )
    {
        int sockfd = events[i].data.fd;
        if ( sockfd == listenfd )
        {
            // 获得客户端的 IP 地址，接受客户端的数据
            struct sockaddr_in client_address;
            socklen_t client_addrlength = sizeof( client_address );
            // accept 成功则返回一个新的 socket，用该 socket 来与被接受连接的客户端进行通信
            int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
            /* 将文件描述符 fd 上的 EPOLLIN 注册到 epollfd 指示的 epoll 内核事件表中，不启用 ET 模式。 */
            addfd( epollfd, connfd, false );
        }
        else if ( events[i].events & EPOLLIN )
        {
            // 只要 socket 读缓存中还有未读出的数据，这段代码就被触发
            printf( "event trigger once\n" );
            memset( buf, '\0', BUFFER_SIZE );
            // 读取 sockfd 上 buf 上的数据
            int ret = recv( sockfd, buf, BUFFER_SIZE-1, 0 );
            if( ret <= 0 )
            {
                close( sockfd );
                continue;
            }
            // 打印读取到的数据
            printf( "get %d bytes of content: %s\n", ret, buf );
        }
        else
        {
            printf( "something else happened \n" );
        }
    }
}

/* ET 模式的工作流程 */
void et( epoll_event* events, int number, int epollfd, int listenfd )
{
    char buf[ BUFFER_SIZE ];
    for ( int i = 0; i < number; i++ )
    {
        int sockfd = events[i].data.fd;
        if ( sockfd == listenfd )
        {
            // 获得客户端的 IP 地址，接受客户端的数据
            struct sockaddr_in client_address;
            socklen_t client_addrlength = sizeof( client_address );
            // accept 成功则返回一个新的 socket，用该 socket 来与被接受连接的客户端进行通信
            int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
            // 对 connfd 开启 ET 模式
            addfd( epollfd, connfd, true );
        }
        // 事件存在，且数据可写
        else if ( events[i].events & EPOLLIN )
        {
            // 这段代码不会被重复触发，所以我们循环读取数据，以确保把 socket 读缓存中的所有数据读出
            printf( "event trigger once\n" );
            while( 1 )
            {
                memset( buf, '\0', BUFFER_SIZE );
                // 读取 sockfd 上 buf 上的数据
                int ret = recv( sockfd, buf, BUFFER_SIZE-1, 0 );
                if( ret < 0 )
                {
                    // 对于非阻塞 IO，下面的条件成立表示数据已经被全部读取完毕。此后，epoll 就能再次触发 sockfd 上的 EPOLLIN 事件，以驱动下一次读操作
                    if( ( errno == EAGAIN ) || ( errno == EWOULDBLOCK ) )
                    {
                        printf( "read later\n" );
                        break;
                    }
                    close( sockfd );
                    break;
                }
                else if( ret == 0 )
                {
                    close( sockfd );
                }
                else
                {
                    printf( "get %d bytes of content: %s\n", ret, buf );
                }
            }
        }
        else
        {
            printf( "something else happened \n" );
        }
    }
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

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, true );

    while( 1 )
    {
        int ret = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ret < 0 )
        {
            printf( "epoll failure\n" );
            break;
        }

        // 使用 LT 模式
        lt( events, ret, epollfd, listenfd );
        // 使用 ET 模式
        //et( events, ret, epollfd, listenfd );
    }

    close( listenfd );
    return 0;
}