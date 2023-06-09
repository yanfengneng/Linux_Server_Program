#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

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
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* 描述一个子进程的类，m_pid 是目标子进程的 PID，m_pipefd 是父进程和子进程通信用的管道。 */
class process
{
public:
    process() : m_pid( -1 ){}

public:
    pid_t m_pid;
    int m_pipefd[2];
};

/* 进程池类：将它定义为模板类是为了代码复用，其模板参数是处理逻辑任务的类。 */
template< typename T >
class processpool
{
private:
    /* 将构造函数定义为私有的，因此我们只能通过后面的 create 静态函数来创建 processpool 实例。 */
    processpool( int listenfd, int process_number = 8 );
public:
    /* 单体模式：以保证程序最多创建一个 processpool 实例，这是程序正确处理信号的必要条件。 */
    static processpool< T >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance )// m_instance 为 0，就创建一个进程池
        {
            m_instance = new processpool< T >( listenfd, process_number );
        }
        return m_instance;
    }
    /* 析构函数：使用所有子进程的描述信息 */
    ~processpool()
    {
        delete [] m_sub_process;
    }

    /* 启动进程池 */
    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    // 进程池允许的最大子进程数量
    static const int MAX_PROCESS_NUMBER = 16;
    // 每个子进程最多能处理的客户数量
    static const int USER_PER_PROCESS = 65536;
    // epoll 最多能处理的事件数
    static const int MAX_EVENT_NUMBER = 10000;
    // 进程池中的进程总数
    int m_process_number;
    // 子进程在池中的序号，从 0 开始
    int m_idx;
    // 每个进程都有一个 epoll 内核事件表，用 m_epollfd 标识
    int m_epollfd;
    // 监听 socket
    int m_listenfd;
    // 子进程通过 m_stop 来决定是否停止运行
    int m_stop;
    // 保存所有子进程的描述信息
    process* m_sub_process;
    // 进程池静态实例
    static processpool< T >* m_instance;
};

// 进程池静态实例初始化
template< typename T >
processpool< T >* processpool< T >::m_instance = NULL;

/* 用于处理信号的管道，以实现统一事件源。后面称之为信号管道。 */
static int sig_pipefd[2];

/* 将文件描述符设置为非阻塞的 */
static int setnonblocking( int fd )
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
static void addfd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    // 启用 ET 模式
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    // 将 fd 设置为非阻塞的
    setnonblocking( fd );
}

/* epollfd 标识的 epoll 内核时间表中删除 fd 上的所有注册事件。 */
static void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

/* 信号处理函数 */
static void sig_handler( int sig )
{
    /* 保留原来的 errno，在函数最后恢复，以保证函数的可重入性 */
    int save_errno = errno;
    int msg = sig;
    /* 将信号值写入管道，以通知主循环 */
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}

/* 设置信号的处理函数 */
static void addsig( int sig, void( handler )(int), bool restart = true )
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    // 指定信号处理函数
    sa.sa_handler = handler;
    if( restart )
    {
        // 将程序收到信号时的行为设置为 "重新调用被该信号终止的系统调用"
        sa.sa_flags |= SA_RESTART;
    }
    // 将信号集中所有信号设置为 sa 的信号掩码
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

/* 进程池构造函数：参数 listenfd 是监听 socket，它必须在创建进程池之前被创建，否则子进程无法直接引用它。参数 process_number 指定进程池中子进程的数量。 */
template< typename T >
processpool< T >::processpool( int listenfd, int process_number ) 
    : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );

    // 创建 process_number 个子进程
    m_sub_process = new process[ process_number ];
    assert( m_sub_process );

    /* 创建 process_number 个子进程，并建立它们和父进程之间的管道。 */
    for( int i = 0; i < process_number; ++i )
    {
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd );
        assert( ret == 0 );

        // 创建子进程
        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if( m_sub_process[i].m_pid > 0 )// 是父进程，则关闭父进程中的写管道
        {
            close( m_sub_process[i].m_pipefd[1] );
            continue;
        }
        else// 是子进程，则关闭子进程中的读管道
        {
            close( m_sub_process[i].m_pipefd[0] );
            m_idx = i;
            break;
        }
    }
}

/* 统一事件源 */
template< typename T >
void processpool< T >::setup_sig_pipe()
{
    /* 创建 epoll 事件监听表和信号管道。 */
    m_epollfd = epoll_create( 5 );
    assert( m_epollfd != -1 );

    int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, sig_pipefd );
    assert( ret != -1 );

    // 写管道设置为非活动连接
    setnonblocking( sig_pipefd[1] );
    // 向 m_epollfd 上注册读事件
    addfd( m_epollfd, sig_pipefd[0] );

    /* 设置信号处理函数 */
    addsig( SIGCHLD, sig_handler );
    addsig( SIGTERM, sig_handler );
    addsig( SIGINT, sig_handler );
    addsig( SIGPIPE, SIG_IGN );
}

/* 父进程中 m_idx 值为 -1，子进程中 m_idx 值大于等于 0，据此可以判断接下来要运行的是父进程代码还是子进程代码了。 */
template< typename T >
void processpool< T >::run()
{
    if( m_idx != -1 )// 运行子进程
    {
        run_child();
        return;
    }
    // 运行父进程
    run_parent();
}

template< typename T >
void processpool< T >::run_child()
{
    // 先进行统一事件源
    setup_sig_pipe();

    /* 每个子进程都通过其在进程池中的序号值 m_idx 找到与父进程通信的管道。*/
    int pipefd = m_sub_process[m_idx].m_pipefd[ 1 ];
    /* 子进程需要监听管道文件描述符 pipefd，因为父进程将通过它来通知子进程 accept 新连接。 */
    addfd( m_epollfd, pipefd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    T* users = new T [ USER_PER_PROCESS ];
    assert( users );
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        // 监听就绪的文件描述符
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            if( ( sockfd == pipefd ) && ( events[i].events & EPOLLIN ) )
            {
                int client = 0;
                /* 从父、子进程之间的管道读取数据，并将结果保存在变量 client 中，如果读取成功，则表示有新客户连接的到来。 */
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 ) 
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
                    // 接受子进程连接
                    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if ( connfd < 0 )
                    {
                        printf( "errno is: %d\n", errno );
                        continue;
                    }
                    // 向 m_epollfd 上注册 connfd 上的事件
                    addfd( m_epollfd, connfd );
                    /* 模板类 T 必须实现 init 方法，以初始化一个客户连接。这样可以直接使用 connfd 来索引逻辑处理对象（T类型的对象），来提高程序效率。 */
                    users[connfd].init( m_epollfd, connfd, client_address );
                }
            }
            /* 下面处理子进程接收到的信号 */
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                // 子进程读取读管道的信号
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:// 子进程状态发送变化
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:// 中断进程信号
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            /* 如果是其他可读数据，那么必然是客户请求到来。调用逻辑处理对象的 process 方法处理之。 */
            else if( events[i].events & EPOLLIN )
            {
                 users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }

    // 释放所有客户资源
    delete [] users;
    users = NULL;
    close( pipefd );
    /* 这句话被注解掉是用来提醒我们的：应该右 m_listenfd 的创建者来关闭这个文件描述符，即所谓的“对象”（比如一个文件描述符、又或者是一段堆内存）由哪个函数创建，就应该由哪个函数销毁。 */
    //close( m_listenfd );
    close( m_epollfd );
}

template< typename T >
void processpool< T >::run_parent()
{
    // 先进行统一事件源
    setup_sig_pipe();

    /* 父进程监听 m_listenfd */
    addfd( m_epollfd, m_listenfd );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
        // 监听就绪的文件描述符
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            /* 如果有新连接到来，就采用 Round Robin 方式将其分配给一个子进程处理。 */
            if( sockfd == m_listenfd )
            {
                int i =  sub_process_counter;
                do
                {
                    if( m_sub_process[i].m_pid != -1 )// 遇到子进程就退出循环
                    {
                        break;
                    }
                    // 取模表示轮转算法
                    i = (i+1)%m_process_number;
                }
                while( i != sub_process_counter );
                
                // 遇到子进程，stop 设置为 true
                if( m_sub_process[i].m_pid == -1 )
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;
                //send( m_sub_process[sub_process_counter++].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                // 向读管道发送信息
                send( m_sub_process[i].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                printf( "send request to child %d\n", i );
                //sub_process_counter %= m_process_number;
            }
            // 下面处理父进程接收到的信号
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
                // 读取读管道的信号
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
                            case SIGCHLD:// 子进程状态发生变化
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
                                    for( int i = 0; i < m_process_number; ++i )
                                    {
                                        // 如果进程池中第 i 个子进程退出了，则主进程关闭相应的通信管道，并设置相应的 m_pid 为 -1，以标记该子进程已经退出。
                                        if( m_sub_process[i].m_pid == pid )
                                        {
                                            printf( "child %d join\n", i );
                                            // 关闭子进程的读管道
                                            close( m_sub_process[i].m_pipefd[0] );
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                /* 如果所有子进程都已经推出了，则父进程也退出。 */
                                m_stop = true;
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    if( m_sub_process[i].m_pid != -1 )
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:// 中断进程信号
                            {
                                /* 如果父进程接收到终止信号，那么就杀死所有子进程，并等待它们全部结束。当然通知子进程结束更好的方法是向父、子进程之间的通信管道发送特殊数据。 */
                                printf( "kill all the clild now\n" );
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else
            {
                continue;
            }
        }
    }

    /* 由创建者来关闭这个文件描述符。 */
    //close( m_listenfd );
    close( m_epollfd );
}

#endif