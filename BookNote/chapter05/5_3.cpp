#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool stop = false;

/* sigterm 信号的处理函数，触发时结束主程序中的循环*/
static void handle_term(int sig)
{
    stop = true;
}

int main(int argc,char* argv[])
{
    // linux 使用 signal() 来安装信号，第一个参数指定要安装的信号，第二个参数指定信号的处理函数
    signal(SIGTERM, handle_term);

    if(argc <= 3){
        printf("usage: %s ip_address port_number backlog\n",basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int backlog = atoi(argv[3]);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    /*创建一个 IPv4 socket 地址*/
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    // 使用 IPv4
    address.sin_family = AF_INET;
    // 将字符串 ip 地址转换为网络字节序正数表示的 ip 地址，并把转换结果存储在 address.sin_addr 指向的内存中。
    inet_pton(AF_INET, ip, &address.sin_addr);
    // 设置端口号
    address.sin_port = htons(port);

    /*  
        命名 socket：将 address 所指的 socket 地址分配给未命名的 sock 文件描述符，使用sizeof 来指出该地址的长度
        bind 成功返回 0，失败返回 -1 并设置 errno。
    */
    int ret = bind(sock, (struct sockaddr*)&address, sizeof(address));
    // 失败就直接断言退出程序了
    assert(ret != -1);

    /*监听 socket：监听 sock，使用 backlog 来提示内核监听队列的最大长度*/
    ret = listen(sock,backlog);
    // 失败就直接断言退出程序了
    assert(ret != -1);

    /*循环等待连接，直到有 SIGTERM 信号将它中断*/
    while(!stop){
        sleep(1);
    }
    /*关闭 socket*/
    close(sock);
    return 0;
}