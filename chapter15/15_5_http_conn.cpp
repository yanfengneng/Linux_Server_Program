#include "http_conn.h"

// 定义了 HTTP 相应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
// 网站的根目录
const char* doc_root = "/var/www/html";

/* 将文件描述符设置为非阻塞的 */
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

/* 将 fd 上的 EPOLLIN 和 EPOLLET 事件注册到 epollfd 指示的 epoll 内核事件表中，参数 oneshot 指定是否注册 fd 上的 EPOLLONESHOT 事件 */
void addfd( int epollfd, int fd, bool one_shot )
{
    epoll_event event;
    event.data.fd = fd;
    // 将事件设置为可读和 ET 模式
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    // 给 fd 上注册 EPOLLONESHOT 事件
    // 对于注册了 EPOLLONESHOT 事件的文件描述符，OS 最多触发其上注册的一个可读、可写或者异常事件，且触发一次，除非使用 epoll_ctl 函数重置该文件描述符上注册的 EPOLLONESHOT 事件
    if( one_shot )
    {
        event.events |= EPOLLONESHOT;
    }
    // 往事件表中注册 fd 上的事件
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    // 将文件描述符设置为非阻塞的
    setnonblocking( fd );
}

/* 将事件表 epollfd 上注册 fd 的事件都进行删除，然后关闭 fd 文件描述符。 */
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

/* 修改事件表 epollfd 上 fd 注册的事件，也就是加上 ev 操作。 */
void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/* 关闭连接 */
void http_conn::close_conn( bool real_close )
{
    if( real_close && ( m_sockfd != -1 ) )
    {
        /* 删除 m_sockfd 这个 socket 连接 */
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        // 关闭一个连接时，将客户数量减 1
        m_user_count--;
    }
}

/* 初始化并接受新的连接 */
void http_conn::init( int sockfd, const sockaddr_in& addr )
{
    m_sockfd = sockfd;
    m_address = addr;
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt( m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len );
    // 如下两行是为了避免 TIME_WAIT 状态，仅用于调试，实际使用时应该去掉
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    // 在事件表 m_epollfd 上注册 sockfd 的事件
    addfd( m_epollfd, sockfd, true );
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始化主状态机当前所处的状态
    m_linger = false;                           // HTTP 请求初始化为不保持连接

    m_method = GET;                             // 请求方法初始化为 GET
    m_url = 0;                                  // 客户请求的目标文件的文件名
    m_version = 0;                              // HTTP 协议版本号
    m_content_length = 0;                       // HTTP 请求的消息体的长度
    m_host = 0;                                 // 主机名
    m_start_line = 0;                           // 当前正在解析的行的起始位置
    m_checked_idx = 0;                          // 当前正在分析的字符在读缓冲区中的位置
    m_read_idx = 0;                             // 标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    m_write_idx = 0;                            // 写缓冲区中待发送的字节数
    memset( m_read_buf, '\0', READ_BUFFER_SIZE );   // 读缓冲区初始化
    memset( m_write_buf, '\0', WRITE_BUFFER_SIZE ); // 写缓冲区初始化
    memset( m_real_file, '\0', FILENAME_LEN );      // 客户请求的目标文件的完整路径的初始化
}

/* 从状态机：参考 8.6 节 */
/* 从状态机，用于解析出一行内容 */
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    /* checked_index 指向 buffer（应用程序的读缓冲区）中当前正在分析的字节，read_index 指向 buffer 中客户数据的尾后的下一字节,也就是说 read_index 是不指向真正的字节的，
    buffer 中第 0~checked_index-1 字节都已分析完毕，第 checked_index~(read_index-1) 字节由下面的循环挨个分析 */
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )
    {
        /* 获得当前要分析的字节 */
        temp = m_read_buf[ m_checked_idx ];
        /* 如果当前的字节是 "\r"，即回车符，则说明可能读取到一个完整的行 */
        if ( temp == '\r' )
        {
            /* 如果"\r"字符碰巧是目前 buffer 中的最后一个已经被读入的客户数据，那么这次分析没有读取到一个完整的行，返回 LINE_OPEN 以表示还需要继续读取客户数据才能进一步分析 */
            if ( ( m_checked_idx + 1 ) == m_read_idx )
            {
                return LINE_OPEN;
            }
            /* 如果下一个字符是"\n"，则说明我们成功读取到一个完整的行 */
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' )
            {
                // 将当前位置和下一个位置都设置为字符串结束符
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }

            return LINE_BAD;
        }
        /* 如果当前的字节是"\n"，即换行符，则也说明可能读取到一个完整的行 */
        else if( temp == '\n' )
        {
            // 由[回车符、换行符]组成表示读取到一个完全的行了。然后将当前位置和上一个位置都设置为字符串结束符。
            if( ( m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) )
            {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    /* 如果所有内容都分析完毕也没遇到"/r"字符，则返回 LINE_OPEN，表示还需要继续读取客户数据才能进一步分析 */
    return LINE_OPEN;
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
    if( m_read_idx >= READ_BUFFER_SIZE )
    {
        return false;
    }

    int bytes_read = 0;
    while( true )
    {
        // 读取客户端数据
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );
        if ( bytes_read == -1 )// 读取数据失败
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return false;
        }
        else if ( bytes_read == 0 )// 客户端关闭连接，返回 false
        {
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
}

// 解析 HHTP 请求行，获得请求方法，目标 URL，以及 HTTP 版本号
http_conn::HTTP_CODE http_conn::parse_request_line( char* text )
{
    // 在 text 中找到" \t"第一次出现的位置
    m_url = strpbrk( text, " \t" );
     /* 如果请求行中没有空白字符或者"\t"字符，则 HTTP 请求必有问题 */
    if ( ! m_url )
    {
        return BAD_REQUEST;
    }
    /* 找到空白字符或者"\t"字符了，则将其设置为字符串结束符，并将 m_url 移动到下一个位置。 */
    *m_url++ = '\0';

    char* method = text;
    /* 仅支持 GET 方法 */
    if ( strcasecmp( method, "GET" ) == 0 )// 比较 szMethod 与 "GET" 字符串，比较时会自动忽略大小写。两个字符串相等则返回 0，则就返回非 0 值。
    {
        m_method = GET;
    }
    /* 不支持 GET 方法，则该 HTTP 请求有问题 */
    else
    {
        return BAD_REQUEST;
    }

    // 检索字符串 m_url 中第一个不在字符串 " \t" 中出现的字符下标。也就是跳过 m_url 中连续的空白字符和回车符
    m_url += strspn( m_url, " \t" );
    // 在 m_version 中找到" \t"第一次出现的位置
    m_version = strpbrk( m_url, " \t" );
    // 若版本号中没有空白字符或'\t'字符，则 HTTP 请求必有问题
    if ( ! m_version )
    {
        return BAD_REQUEST;
    }
    /* 找到空白字符或者"\t"字符了，则将其设置为字符串结束符，并将 m_version 移动到下一个位置。 */
    *m_version++ = '\0';
    // 检索字符串 m_version 中第一个不在字符串 " \t" 中出现的字符下标。也就是跳过 m_version 中连续的空白字符和回车符
    m_version += strspn( m_version, " \t" );
    // 判断版本号是否是"HTTP/1.1"。若不是，则直接返回该 HTTP 请求有问题
    if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 )
    {
        return BAD_REQUEST;
    }

    // 检查 URL 是否合法
    if ( strncasecmp( m_url, "http://", 7 ) == 0 )
    {
        m_url += 7;// 右移7位
        m_url = strchr( m_url, '/' );// m_url 移动到'/'在 m_url 第一次出现的位置
    }

    // m_url 为空或者第一个字符不为'/'，则该 HTTP 请求有问题
    if ( ! m_url || m_url[ 0 ] != '/' )
    {
        return BAD_REQUEST;
    }

    // HTTP 请求行处理完毕，状态转移到头部字段的分析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析 HTTP 请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers( char* text )
{
    // 遇到空行，表示头部字段解析完毕
    if( text[ 0 ] == '\0' )
    {
        
        if ( m_method == HEAD )
        {
            return GET_REQUEST;// 返回得到请求
        }
        // 如果 HTTP 请求有消息体，则还需要读取 m_content_length 字节的消息体，状态机转移到 CHECK_STATE_CONTENT 状态
        if ( m_content_length != 0 )
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        // 否则说明我们已经得到了一个完整的 HTTP 请求
        return GET_REQUEST;
    }
    // 处理 Connection 头部字段
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
    {
        text += 11;// 右移11位
        text += strspn( text, " \t" );// text 跳过连续的空白字符或回车符
        if ( strcasecmp( text, "keep-alive" ) == 0 )// 是否是保持连接
        {
            m_linger = true;// 保持连接
        }
    }
    // 处理 Content-Length 头部字段
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 )
    {
        text += 15;// 右移15位
        text += strspn( text, " \t" );// text 跳过连续的空白字符或回车符
        m_content_length = atol( text );// 将 text 内容转换为 long 整数
    }
    // 处理 Host 头部字段
    else if ( strncasecmp( text, "Host:", 5 ) == 0 )
    {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;// 获得主机名
    }
    else
    {
        printf( "oop! unknow header %s\n", text );
    }

    return NO_REQUEST;

}

// 我们没有真正解析 HTTP 请求的消息体，只是判断它是否被完全地读入了
http_conn::HTTP_CODE http_conn::parse_content( char* text )
{
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

// 主状态机：其分析参考 8.6
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;  // 记录当前行的读取状态
    HTTP_CODE ret = NO_REQUEST;         // 记录 HTTP 请求的处理结果
    char* text = 0;

    while ( ( ( m_check_state == CHECK_STATE_CONTENT ) && ( line_status == LINE_OK  ) )
                || ( ( line_status = parse_line() ) == LINE_OK ) )
    {
        text = get_line();// 得到当前行的内容
        m_start_line = m_checked_idx;// 行的起始位置
        printf( "got 1 http line: %s\n", text );

        switch ( m_check_state )// m_check_state 记录主状态机当前的状态
        {
            /* 第一个状态：分析请求行 */
            // 当前的状态是 CHECK_STATE_REQUESTLINE，则表示 parse_line 函数解析出来的是请求行，于是主状态机调用 parse_request_line 来分析请求行
            case CHECK_STATE_REQUESTLINE:
            {
                // 分析请求行
                // 只有在 parse_requestline 函数在成功分析完请求行之后将其设置为 CHECK_STATE_HEADER，从而实现状态转移
                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST )
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
                ret = parse_headers( text );
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                break;
            }
            /* 第三个状态：分析内容字段 */
            // 当前的状态是 CHECK_STATE_CONTENT，则表示 parse_line 函数解析出来的是内容字段，欲使主状态机调用 parse_content 来分析内容字段
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content( text );
                if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                // 返回服务器内部错误
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

/* 当得到一个完整、正确的 HTTP 请求时，我们就分析目标文件的属性。
如果目标文件存在，对所有用户可读，且不是目录，则使用 mmap 将其映射到内存地址 m_file_addres 处，并告诉调用者获取文件成功。 */
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    if ( stat( m_real_file, &m_file_stat ) < 0 )
    {
        return NO_RESOURCE;
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )
    {
        return FORBIDDEN_REQUEST;
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )
    {
        return BAD_REQUEST;
    }

    int fd = open( m_real_file, O_RDONLY );
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

/* 对内存映射区执行 munmap 操作 */
void http_conn::unmap()
{
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;
    }
}

/* 写 HTTP 响应 */
bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if ( bytes_to_send == 0 )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        init();
        return true;
    }

    while( 1 )
    {
        temp = writev( m_sockfd, m_iv, m_iv_count );
        if ( temp <= -1 )
        {
            // 如果 TCP 写缓冲没有空间，则等待下一轮 EPOLLOUT 事件。虽然在此期间，服务器无法理解接收到同一客户的下一个请求，但这可以保证连接的完整性。
            if( errno == EAGAIN )
            {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if ( bytes_to_send <= bytes_have_send )
        {
            // 发送 HTTP 响应成功，根据 HTTP 请求中的 Connection 字段决定是否立即关闭连接
            unmap();
            if( m_linger )
            {
                init();
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            }
            else
            {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format, ... )
{
    if( m_write_idx >= WRITE_BUFFER_SIZE )
    {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) )
    {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

/* 添加状态行 */
bool http_conn::add_status_line( int status, const char* title )
{
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

/* 添加头部字段 */
bool http_conn::add_headers( int content_len )
{
    add_content_length( content_len );
    add_linger();
    add_blank_line();
}

/* 添加内容字段 */
bool http_conn::add_content_length( int content_len )
{
    return add_response( "Content-Length: %d\r\n", content_len );
}

/* 添加保持连接字段 */
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

/* 添加空白行 */
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

/* 添加字符串内容 */
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

// 根据服务器处理 HTTP 请求的结果，决定返回给客户端的内容
bool http_conn::process_write( HTTP_CODE ret )
{
    switch ( ret )
    {
        case INTERNAL_ERROR:// 服务器内部错误
        {
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) )
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:// 请求错误
        {
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) )
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:// 无资源
        {
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) )
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:// 无权限
        {
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( ! add_content( error_403_form ) )
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:// 文件请求成功
        {
            add_status_line( 200, ok_200_title );
            if ( m_file_stat.st_size != 0 )
            {
                // 添加头部字段
                add_headers( m_file_stat.st_size );
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else
            {
                // 返回网页内容
                const char* ok_string = "<html><body></body></html>";
                add_headers( strlen( ok_string ) );
                if ( ! add_content( ok_string ) )
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 由线程池中的工作线程调用，这是处理 HTTP 请求的入口函数
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if ( read_ret == NO_REQUEST )
    {
        // 向 m_epollfd 上注册 m_sockfd 上的写事件
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    bool write_ret = process_write( read_ret );
    if ( ! write_ret )
    {
        close_conn();// 关闭客户端连接
    }
    // 向 m_epollfd 上注册 m_sockfd 上的读事件
    modfd( m_epollfd, m_sockfd, EPOLLOUT );
}