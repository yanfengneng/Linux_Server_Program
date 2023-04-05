#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

/* 发送带外数据 */
int main(int argc,char* argv[])
{
    if(argc <= 2)
    {
        printf("usage: %s ip_address port_number\n",basename(argv[0]));
        return 1;
    }

    /* 获得 ip 地址 */
    const char* ip = argv[1];
    /* 获得端口号 */
    int port = atoi(argv[2]);

    struct sockaddr_in server_address;
    // 将服务器 ip 地址初始化为 0
    bzero(&server_address,sizeof(server_address));
    // 服务器的地址族设置为 IPv4 地址
    server_address.sin_family=AF_INET;

    /* 将参数 ip 的字符串IP地址转换为用大端方式整数表示的IP地址存放在 server_address.sin_addr 指向的内存中，并使用 IPv4 的地址族 */
    inet_pton(AF_INET,ip,&server_address.sin_addr);
    /* 将小端方式的端口号转换为大端方式的端口号，然后赋值给服务器的端口号 */
    server_address.sin_port=htons(port);

    /* 创建 socket：使用 UNIX 本地域协议族；传输层使用 TCP 协议；最后一个参数使用默认协议 */
    int sockfd=socket(PF_INET,SOCK_STREAM,0);
    /* 创建 socket 失败就发生断言 */
    assert(sockfd>=0);
    /* 服务端开始监听 socket：指定监听服务器的 IP 地址，然后指定服务器的长度。connet失败就会返回 -1，这样程序就会推出了。 */
    if(connect(sockfd,(struct sockaddr*)&server_address,sizeof(server_address))<0){
        printf("connection failed\n");
    }
    /* 服务器监听成功 */
    else
    {
        const char* oob_data = "abc";
        const char* normal_data = "123";
        /* 往 sockfd 上写入数据 normal_data，然后将字符串的位置和大小进行指定，然后使用默认 flag 为 0。 */
        send(sockfd,normal_data,strlen(normal_data),0);
        /* 往 sockfd 上写入数据 oob_data，然后将字符串的位置和大小进行指定，flag 使用 MSG_OOB，表示发送或接收紧急数据。 */
        send(sockfd,oob_data,strlen(oob_data),MSG_OOB);
        /* 往 sockfd 上写入数据 normal_data，然后将字符串的位置和大小进行指定，然后使用默认 flag 为 0。 */
        send(sockfd,normal_data,strlen(normal_data),0);
    }
    /* 关闭 socket 连接 */
    close(sockfd);
    return 0;
}