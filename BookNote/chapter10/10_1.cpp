#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
static int pipefd[2];

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

/* 将文件描述符 fd 上的 EPOLLIN 注册到 epollfd 指示的 epoll 内核事件表中 */
void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

/* 信号处理函数 */
void sig_handler( int sig )
{
    /* 保留原来的 errno，在函数最后恢复，以保证函数的可重入性 */
    int save_errno = errno;
    int msg = sig;
    /* 将信号值写入管道，以通知主循环 */
    send( pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

/* 设置信号的处理函数 */
void addsig( int sig )
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

/* 服务器程序 */
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

    // 创建 socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    // 绑定 socket 地址
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    if( ret == -1 )
    {
        printf( "errno is %d\n", errno );
        return 1;
    }
    
    // 开始监听 socket，并限制最大监听数为 5 个
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    // 向 epollfd 的事件表上注册 listenfd 事件
    addfd( epollfd, listenfd );

    /* 使用 socketpair 创建管道，注册 pipefd[0] 上的可读事件 */
    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert( ret != -1 );
    // 将写管道设置为非阻塞的
    setnonblocking( pipefd[1] );
    // 在 epollfd 上注册 pipefd[0] 上的可读事件，并启用 ET 模式
    addfd( epollfd, pipefd[0] );

    /* 设置一些信号的处理函数 */
    addsig( SIGHUP );   // 控制终端挂起
    addsig( SIGCHLD );  // 子进程状态发生变化（退出或暂停）
    addsig( SIGTERM );  // 终止进程。kill 命令默认发送的信号就是 SIGTERM
    addsig( SIGINT );   // 键盘输入使进程退出
    
    // 是否暂停服务
    bool stop_server = false;

    while( !stop_server )
    {
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
                // 往 epollfd 上注册 connfd 的可读事件和 ET 模式
                addfd( epollfd, connfd );
            }
            // 如果就绪的文件描述符是 pipefd[0]，则处理信号
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                // 接收读管道上的信号
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 )
                {
                    continue;
                }
                else if( ret == 0 )
                {
                    continue;
                }
                else
                {
                    // 因为每个信号值占 1 字节，所以按字节来逐个接收信号。以 SIGTERM 为例，来说明如何安全地终止服务器主循环的
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:       // 子进程状态发生变化
                            case SIGHUP:        // 控制终端挂起
                            {
                                continue;
                            }
                            case SIGTERM:       // 终止进程
                            case SIGINT:        // 键盘输入以中断进程
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            else
            {
            }
        }
    }

    printf( "close fds\n" );
    close( listenfd );
    close( pipefd[1] );
    close( pipefd[0] );
    return 0;
}