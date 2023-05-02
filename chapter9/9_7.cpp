#define _GNU_SOURCE 1
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
#include <poll.h>

// 最大用户数量
#define USER_LIMIT 5
// 该缓冲区的大小
#define BUFFER_SIZE 64
// 文件描述符数量限制
#define FD_LIMIT 65535

// 客户数据：客户端 socket 地址、待写到客户端的数据的位置、从客户端读入的数据
struct client_data
{
    sockaddr_in address;
    char* write_buf;
    char buf[ BUFFER_SIZE ];
};

/* 将文件描述符设置成非阻塞的 */
int setnonblocking( int fd )
{
    // 6.8 fcntl 函数
    /* 获取 fd 的状态标志，这些标志包括由 open 系统调用设置的标志和访问模式 */
    int old_option = fcntl( fd, F_GETFL );
    // 加上无锁标志
    int new_option = old_option | O_NONBLOCK;
    // 给 fd 设置新标志
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

// 服务器
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
    // 服务器本地 socket 地址的初始化
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    // 创建 socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );

    // 将本地文件描述符与本地服务器 socket 地址进行绑定
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    // 开始监听 listenfd 绑定的 socket 地址，最多连接 5 个客户端
    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    /* 创建 users 数组，分配 FD_LIMIT 个 client_data 对象。可以预期：每个可能的 socket 连接都可以获得一个这样的对象，并且 socket 的值可以
    直接用来索引（作为数组的下标）socket 连接对应的 client_data 对象，这样将 socket 和客户数据关联的简单而高效的方式 */
    client_data* users = new client_data[FD_LIMIT];
    // 尽管分配了足够多的 client_data 对象，但是为了提高 poll 的性能，依然有必要限制用户的数量
    pollfd fds[USER_LIMIT+1];
    int user_counter = 0;
    // 初始化 epoll 结构体
    for( int i = 1; i <= USER_LIMIT; ++i )
    {
        fds[i].fd = -1;
        fds[i].events = 0;
    }
    // fds[0] 初始化为本地的文件描述符 listenfd，使用 listenfd 来与客户端进行交互
    fds[0].fd = listenfd;
    // 事件初始化为可读和错误
    fds[0].events = POLLIN | POLLERR;
    // 实际发生的事件为 0 个
    fds[0].revents = 0;

    while( 1 )
    {
        // 开始监听事件
        ret = poll( fds, user_counter+1, -1 );
        if ( ret < 0 )
        {
            printf( "poll failure\n" );
            break;
        }
        // 开始遍历用户
        for( int i = 0; i < user_counter+1; ++i )
        {
            if( ( fds[i].fd == listenfd ) && ( fds[i].revents & POLLIN ) )
            {
                // 开始与客户端进行连接
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                /* 从监听队列中接受一个连接，然后服务器来获取被接受连接的远程 socket 地址
                监听成功的话，accept返回一个 socket，可以使用该 socket 进行通信。*/
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                // 如果请求太多，则关闭新到的连接
                if( user_counter >= USER_LIMIT )
                {
                    const char* info = "too many users\n";
                    printf( "%s", info );
                    // 给客户端发送数据，然后关闭 socket 连接（读写和发送数据都是通过文件描述符来进行的，这也就是 linux 一切皆文件的原因了。）
                    send( connfd, info, strlen( info ), 0 );
                    close( connfd );
                    continue;
                }
                // 对于新的连接，同时修改 fds 和 users 数组。users[connfd] 对应新连接文件描述符 connfd 的客户数据
                user_counter++;
                // 将获取到的客户端 socket 地址赋值给用户数组
                users[connfd].address = client_address;
                // 将 connfd 设置为非阻塞的
                setnonblocking( connfd );
                // 更新新连接的文件描述符，注册的事件，实际发生的事件
                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                printf( "comes a new user, now have %d users\n", user_counter );
            }
            else if( fds[i].revents & POLLERR )
            {
                printf( "get an error from %d\n", fds[i].fd );
                char errors[ 100 ];
                memset( errors, '\0', 100 );
                socklen_t length = sizeof( errors );
                if( getsockopt( fds[i].fd, SOL_SOCKET, SO_ERROR, &errors, &length ) < 0 )
                {
                    printf( "get socket option failed\n" );
                }
                continue;
            }
            else if( fds[i].revents & POLLRDHUP )
            {
                // 如果客户端关闭连接，则服务器也关闭对应的连接，并将用户数减 1
                users[fds[i].fd] = users[fds[user_counter].fd];
                close( fds[i].fd );
                fds[i] = fds[user_counter];
                i--;
                user_counter--;
                printf( "a client left\n" );
            }
            else if( fds[i].revents & POLLIN )
            {
                int connfd = fds[i].fd;
                memset( users[connfd].buf, '\0', BUFFER_SIZE );
                ret = recv( connfd, users[connfd].buf, BUFFER_SIZE-1, 0 );
                printf( "get %d bytes of client data %s from %d\n", ret, users[connfd].buf, connfd );
                if( ret < 0 )
                {
                    // 如果读操作出错，则关闭连接
                    if( errno != EAGAIN )
                    {
                        close( connfd );
                        users[fds[i].fd] = users[fds[user_counter].fd];
                        fds[i] = fds[user_counter];
                        i--;
                        user_counter--;
                    }
                }
                else if( ret == 0 )
                {
                    printf( "code should not come to here\n" );
                }
                else
                {
                    // 如果接收到客户数据，则通知其他 socket 连接准备写数据
                    for( int j = 1; j <= user_counter; ++j )
                    {
                        if( fds[j].fd == connfd )
                        {
                            continue;
                        }
                        
                        fds[j].events |= ~POLLIN;
                        fds[j].events |= POLLOUT;
                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            }
            else if( fds[i].revents & POLLOUT )
            {
                int connfd = fds[i].fd;
                if( ! users[connfd].write_buf )
                {
                    continue;
                }
                //printf("Please the server put in data:\n");
                ret = send( connfd, users[connfd].write_buf, strlen( users[connfd].write_buf ), 0 );
                users[connfd].write_buf = NULL;
                // 写完数据后需要重新注册 fds[i] 上的可读事件
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN;
            }
        }
    }

    delete [] users;
    close( listenfd );
    return 0;
}