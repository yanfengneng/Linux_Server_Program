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
#include "11_2lst_timer.h"

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 5

static int pipefd[2];
// 使用升序链表来管理定时器
static sort_timer_lst timer_lst;
static int epollfd = 0;

/* 将文件描述符设置成非阻塞的 */
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

/* 将文件描述符 fd 上的 EPOLLIN 和 ET 模式注册到 epollfd 指示的 epoll 内核事件表中 */
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
    /* 保留原来的 errno，在函数最后回复，以保证函数的可重入性 */
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

void timer_handler()
{
    /* 定时处理任务，实际上就是调用 tick 函数 */
    timer_lst.tick();
    /* 因为一次 alarm 调用只会引起一次 SIGALRM 信号，所以需要重新定时，以不断触发 SIGALRM 信号 */
    alarm( TIMESLOT );
}

/* 定时器回调函数，删除非活动连接 socket 上的注册事件，并将其关闭 */
void cb_func( client_data* user_data )
{
    // 删除该用户在 epollfd 上注册的事件
    epoll_ctl( epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0 );
    assert( user_data );
    // 关闭该用户 socket
    close( user_data->sockfd );
    printf( "close fd %d\n", user_data->sockfd );
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

    int ret = 0;
    // 初始化 socket 地址
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 创建 socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    // 绑定 socket
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    // 开始监听 socket
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    // 注册事件的初始化
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );
    // 把 listenfd 加入事件表
    addfd( epollfd, listenfd );

    ret = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd );
    assert( ret != -1 );
    // 读管道设置为非活动连接
    setnonblocking( pipefd[1] );
    // 写管道注册到事件表中
    addfd( epollfd, pipefd[0] );

    // 设置信号处理函数
    addsig( SIGALRM );  // 由 alarm 或 setitimer 设置的实时闹钟超时引起
    addsig( SIGTERM );  // 终止进程
    bool stop_server = false;

    client_data* users = new client_data[FD_LIMIT]; 
    bool timeout = false;
    // 定时
    alarm( TIMESLOT );

    while( !stop_server )
    {
        // 开始检测就绪事件
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }
    
        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            // 处理新到的客户连接
            if( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                // 从等待队列中选择一个客户端的 socket，用该 socket 来与被接受连接的客户端进行通信
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                // 将该 socket 的文件描述符注册到事件表中
                addfd( epollfd, connfd );
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表 timer_lst 中
                util_timer* timer = new util_timer;
                timer->user_data = &users[connfd];      // 定时器绑定用户数据
                timer->cb_func = cb_func;               // 回调函数
                time_t cur = time( NULL );              // 当前时间
                timer->expire = cur + 3 * TIMESLOT;     // 设置定时器的超时时间
                users[connfd].timer = timer;            // 将定时器添加到用户表中
                timer_lst.add_timer( timer );           // 将定时器添加到链表中
            }
            // 处理信号
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                // 从读管道中接收数据
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );
                if( ret == -1 ){
                    // handle the error
                    continue;
                }
                else if( ret == 0 ){
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGALRM: // 由 alarm 或 setitimer 设置的实时闹钟超时引起
                            {
                                /* 用 timeout 变量标记有定时任务需要处理，但不立即处理定时任务。这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务 */
                                timeout = true;
                                break;
                            }
                            case SIGTERM: // 终止进程
                            {
                                // 停止服务
                                stop_server = true;
                            }
                        }
                    }
                }
            }
            /* 处理客户连接上接收到的数据 */
            else if(  events[i].events & EPOLLIN )
            {
                memset( users[sockfd].buf, '\0', BUFFER_SIZE );
                // 接收自来客户端的数据
                ret = recv( sockfd, users[sockfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, users[sockfd].buf, sockfd );
                // 获得用户的定时器
                util_timer* timer = users[sockfd].timer;
                if( ret < 0 )
                {
                    /* 如果发生触发，则关闭连接，并移除其对应的定时器 */
                    if( errno != EAGAIN )
                    {
                        cb_func( &users[sockfd] ); // 使用回调函数，删除非活动连接 socket 上的注册事件，并将其关闭
                        if( timer ){
                            timer_lst.del_timer( timer );// 在升序链表上删除该定时器
                        }
                    }
                }
                else if( ret == 0 )
                {
                    /* 如果对方已经关闭连接，则我们也关闭连接，并移除对应的定时器 */
                    cb_func( &users[sockfd] );
                    if( timer ){
                        timer_lst.del_timer( timer ); // 删除定时器
                    }
                }
                else
                {
                    /* 如果某个客户连接上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间 */
                    if( timer )
                    {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;// 延长任务的超时时间
                        printf( "adjust timer once\n" );
                        // 将延长时间之后的定时器加入到链表中
                        timer_lst.adjust_timer( timer );
                    }
                }
            }
            else
            {
                // others
            }
        }

        /* 最后处理定时事件，因为 IO 事件有更高的优先级。但是这样做将会导致定时任务不能精确地按照预期的时间执行。 */
        if( timeout )
        {
            /* 定时处理任务，实际上就是调用 tick 函数 */
            timer_handler();
            timeout = false;
        }
    }

    close( listenfd );  // 关闭文件描述符
    close( pipefd[1] ); // 关闭写信号管道
    close( pipefd[0] ); // 关闭读信号管道
    delete [] users;
    return 0;
}