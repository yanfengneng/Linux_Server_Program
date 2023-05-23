#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

/* 读缓冲区的大小 */
#define BUFFER_SIZE 4096
/* 主状态机的两种可能状态，分别表示：当前正在分析请求行、当前正在分析头部字段 */
enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
/* 从状态机的三种可能状态，即行的读取状态，分别表示：读取到一个完整的行、行出错、行数据尚且不完整 */
enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
/* 服务器处理 HTTP 请求的结果：NO_REQUEST 表示请求不完整，需要继续读取客户数据；
GET_REQUEST 表示获得了一个完整的客户请求；BAD_REQUEST 表示客户请求有语法错误；
FORBIDDEN_REQUEST 表示客户对资源没有足够的访问权限；INTERNAL_ERROR 表示服务器内部错误；
CLOSED_CONNECTION 表示客户端已经关闭连接了 */
enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, FORBIDDEN_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
/* 为了简化问题，我们没有给客户端发送一个完整的 HTTP 应答报文，而只是根据服务器的处理结果发送如下成功或失败的信息 */
static const char* szret[] = { "I get a correct result\n", "Something wrong\n" };

/* 从状态机，用于解析出一行内容 */
LINE_STATUS parse_line( char* buffer, int& checked_index, int& read_index )
{
    char temp;
    /* checked_index 指向 buffer（应用程序的读缓冲区）中当前正在分析的字节，read_index 指向 buffer 中客户数据的尾后的下一字节,也就是说 read_index 是不指向真正的字节的，
    buffer 中第 0~checked_index-1 字节都已分析完毕，第 checked_index~(read_index-1) 字节由下面的循环挨个分析 */
    for ( ; checked_index < read_index; ++checked_index )
    {
        /* 获得当前要分析的字节 */
        temp = buffer[ checked_index ];
        /* 如果当前的字节是 "\r"，即回车符，则说明可能读取到一个完整的行 */
        if ( temp == '\r' )
        {
            /* 如果"\r"字符碰巧是目前 buffer 中的最后一个已经被读入的客户数据，那么这次分析没有读取到一个完整的行，返回 LINE_OPEN 以表示还需要继续读取客户数据才能进一步分析 */
            if ( ( checked_index + 1 ) == read_index )
            {
                return LINE_OPEN;
            }
            /* 如果下一个字符是"\n"，则说明我们成功读取到一个完整的行 */
            else if ( buffer[ checked_index + 1 ] == '\n' )
            {
                // 将当前位置和下一个位置都设置为字符串结束符
                buffer[ checked_index++ ] = '\0';
                buffer[ checked_index++ ] = '\0';
                return LINE_OK;
            }
            /* 否则的话，说明客户发送的 HTTP 请求存在在语法问题 */
            return LINE_BAD;
        }
        /* 如果当前的字节是"\n"，即换行符，则也说明可能读取到一个完整的行 */
        else if( temp == '\n' )
        {
            // 由[回车符、换行符]组成表示读取到一个完全的行了。然后将当前位置和上一个位置都设置为字符串结束符。
            if( ( checked_index > 1 ) &&  buffer[ checked_index - 1 ] == '\r' )
            {
                buffer[ checked_index-1 ] = '\0';
                buffer[ checked_index++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    /* 如果所有内容都分析完毕也没遇到"/r"字符，则返回 LINE_OPEN，表示还需要继续读取客户数据才能进一步分析 */
    return LINE_OPEN;
}

/* 分析请求行 */
HTTP_CODE parse_requestline( char* szTemp, CHECK_STATE& checkstate )
{
    // 在 szURL 中匹配空白字符或者"\t"字符，若未找到字符则返回 NULL。
    char* szURL = strpbrk( szTemp, " \t" );
    /* 如果请求行中没有空白字符或者"\t"字符，则 HTTP 请求必有问题 */
    if ( ! szURL ){
        return BAD_REQUEST;
    }

    /* 找到空白字符或者"\t"字符了，则将其设置为字符串结束符，并将 szURL 移动到下一个位置。 */
    *szURL++ = '\0';

    char* szMethod = szTemp;
    /* 仅支持 GET 方法 */
    if ( strcasecmp( szMethod, "GET" ) == 0 )// 比较 szMethod 与 "GET" 字符串，比较时会自动忽略大小写。两个字符串相等则返回 0，则就返回非 0 值。
    {
        printf( "The request method is GET\n" );
    }
    /* 不支持 GET 方法，则该 HTTP 请求有问题 */
    else
    {
        return BAD_REQUEST;
    }

    // 检索字符串 szURL 中第一个不在字符串 " \t" 中出现的字符下标。也就是跳过 szURL 中连续的空白字符和回车符
    szURL += strspn( szURL, " \t" );
    char* szVersion = strpbrk( szURL, " \t" );
    /* 如果请求行中没有空白字符或者"\t"字符，则 HTTP 请求必有问题 */
    if ( ! szVersion )
    {
        return BAD_REQUEST;
    }
    /* 找到空白字符或者"\t"字符了，则将其设置为字符串结束符，并将 szVersion 移动到下一个位置。 */
    *szVersion++ = '\0';
    // 跳过连续的空白字符或者"\t"字符了
    szVersion += strspn( szVersion, " \t" );
    /* 仅支持 HTTP/1.1 */
    if ( strcasecmp( szVersion, "HTTP/1.1" ) != 0 ) // 这两个字符串相同就返回0，不相同返回非 0 值。
    {
        // 不相同返回 HTTP 请求有问题
        return BAD_REQUEST;
    }

    /* 检查 URL 是否合法 */ 
    if ( strncasecmp( szURL, "http://", 7 ) == 0 )// 合法
    {
        szURL += 7;// 右移7位
        szURL = strchr( szURL, '/' );// szURL 移动到'/'在 szURL 第一次出现的位置
    }

    // szURL 为空或者第一个字符不为'/'，则该 HTTP 请求有问题
    if ( ! szURL || szURL[ 0 ] != '/' )
    {
        return BAD_REQUEST;
    }

    // 最终得到 HTTP 请求的 URL 了
    printf( "The request URL is: %s\n", szURL );
    /* HTTP 请求行处理完毕，状态转移到头部字段的分析 */
    checkstate = CHECK_STATE_HEADER;
    // 返回请求不完整，需要继续读取客户数据
    return NO_REQUEST;
}

/* 分析头部字段 */
HTTP_CODE parse_headers( char* szTemp )
{
    /* 遇到一个空行，说明得到了一个正确的 HTTP 请求 */
    if ( szTemp[ 0 ] == '\0' )
    {
        return GET_REQUEST;
    }
    /* 解析到"Host:"字段，然后将 szTemp 右移 5 位，跳过连续的空格和'\t'字符，打印出主机地址。 */
    else if ( strncasecmp( szTemp, "Host:", 5 ) == 0 )
    {
        szTemp += 5;
        szTemp += strspn( szTemp, " \t" );
        printf( "the request host is: %s\n", szTemp );
    }
    /* 其他头部字段都不处理 */
    else
    {
        printf( "I can not handle this header\n" );
    }

    // 返回请求不完整字段
    return NO_REQUEST;
}

/* 分析 HTTP 请求的入口函数 */
HTTP_CODE parse_content( char* buffer, int& checked_index, CHECK_STATE& checkstate, int& read_index, int& start_line )
{
    /* 记录当前行的读取状态 */
    LINE_STATUS linestatus = LINE_OK;
    /* 记录 HTTP 请求的处理结果 */
    HTTP_CODE retcode = NO_REQUEST;
    /* 主状态机，用于从 buffewr 中取所有完整的行 */
    while( ( linestatus = parse_line( buffer, checked_index, read_index ) ) == LINE_OK )
    {
        /* start_line 是行在 buffer 中的起始位置 */
        char* szTemp = buffer + start_line;
        /* 记录下一行的起始位置 */
        start_line = checked_index;
        /* checkstate 记录主状态机当前的状态 */
        switch ( checkstate )
        {
            /* 第一个状态：分析请求行 */
            // 当前的状态是 CHECK_STATE_REQUESTLINE，则表示 parse_line 函数解析出来的是请求行，于是主状态机调用 parse_requestline 来分析请求行
            case CHECK_STATE_REQUESTLINE:
            {
                // 分析请求行
                // 只有在 parse_requestline 函数在成功分析完请求行之后将其设置位 CHECK_STATE_HEADER，从而实现状态转移
                retcode = parse_requestline( szTemp, checkstate );
                if ( retcode == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                break;
            }
            /* 第二个状态：分析头部字段 */
            // 当前的状态是 CHECK_STATE_HEADER，则表示 parse_line 函数解析出来的是头部字段，欲使主状态机调用 parse_headers 来分析头部字段
            case CHECK_STATE_HEADER:
            {
                // 分析头部字段
                retcode = parse_headers( szTemp );
                if ( retcode == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if ( retcode == GET_REQUEST )
                {
                    return GET_REQUEST;
                }
                break;
            }
            default:
            {
                // 返回服务器内部错误
                return INTERNAL_ERROR;
            }
        }
    }
    // 若没有读取到一个完整的行，则表示还需要继续读取客户数据才能进一步分析 
    if( linestatus == LINE_OPEN ){
        return NO_REQUEST;
    }
    else{
        return BAD_REQUEST;
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
    
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );
    
    // 创建 socket
    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    
    // 绑定 socket 地址
    int ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );
    
    // 开始监听 socket
    ret = listen( listenfd, 5 );
    assert( ret != -1 );
    
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof( client_address );
    // 从等待队列中选择一个客户端 socket 进行通信
    int fd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
    if( fd < 0 )
    {
        printf( "errno is: %d\n", errno );
    }
    else
    {
        // 读缓冲区
        char buffer[ BUFFER_SIZE ];
        memset( buffer, '\0', BUFFER_SIZE );
        int data_read = 0;
        int read_index = 0;     // 当前已经读取了多少字节的客户数据
        int checked_index = 0;  // 当前已经分析完了多少字节的客户数据
        int start_line = 0;     // 行在 buffer 中的起始位置
        /* 设置主状态机的起始状态 */
        CHECK_STATE checkstate = CHECK_STATE_REQUESTLINE;
        /* 循环读取客户数据并分析之 */
        while( 1 )
        {
            // 接收客户端的数据
            data_read = recv( fd, buffer + read_index, BUFFER_SIZE - read_index, 0 );
            if ( data_read == -1 )
            {
                printf( "reading failed\n" );
                break;
            }
            // 没有数据表示客户端已经关闭连接
            else if ( data_read == 0 )
            {
                printf( "remote client has closed the connection\n" );
                break;
            }
    
            read_index += data_read;
            // 分析目前已经获得的所有客户数据
            HTTP_CODE result = parse_content( buffer, checked_index, checkstate, read_index, start_line );
            // 尚未得到一个完整的 HTTP 请求
            if( result == NO_REQUEST )
            {
                continue;
            }
            // 得到一个完整的、正确的 HTTP 请求
            else if( result == GET_REQUEST )
            {
                // 给客户端发送解析成功的信息
                send( fd, szret[0], strlen( szret[0] ), 0 );
                break;
            }
            // 其他情况表示错误发生
            else
            {
                // 给客户端发送解析失败的信息
                send( fd, szret[1], strlen( szret[1] ), 0 );
                break;
            }
        }
        close( fd );
    }
    
    close( listenfd );
    return 0;
}