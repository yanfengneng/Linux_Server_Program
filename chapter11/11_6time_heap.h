#ifndef intIME_HEAP
#define intIME_HEAP

#include <iostream>
#include <netinet/in.h>
#include <time.h>
using std::exception;

#define BUFFER_SIZE 64

/* 前向声明 */
class heap_timer;

// 绑定 socket 和定时器
struct client_data
{
    sockaddr_in address;        // 客户端 socket 地址
    int sockfd;                 // socket 文件描述符
    char buf[ BUFFER_SIZE ];    // 读缓存
    heap_timer* timer;          // 定时器
};

// 定时器类
class heap_timer
{
public:
    // 构造函数
    heap_timer( int delay ){
        expire = time( NULL ) + delay;
    }

public:
   time_t expire;                       // 定时器生效的绝对时间
   void (*cb_func)( client_data* );     // 定时器的回调函数
   client_data* user_data;              // 用户数据
};

// 时间堆类
class time_heap
{
public:
    // 初始化一个大小为 cap 的空集
    time_heap( int cap ) throw ( std::exception ): capacity( cap ), cur_size( 0 )
    {
        array = new heap_timer* [capacity];     // 创建堆数组
        // 申请内存失败，抛出异常
        if ( ! array ){
            throw std::exception();
        }
        // 将每个堆元素设置为空
        for( int i = 0; i < capacity; ++i ){
            array[i] = NULL;
        }
    }

    // 用已有的数组来初始化堆
    time_heap( heap_timer** init_array, int size, int capacity ) throw ( std::exception ): cur_size( size ), capacity( capacity )
    {
        // 已有数组大小比堆容量大，则抛出异常
        if ( capacity < size )
        {
            throw std::exception();
        }
        // 创建堆数组
        array = new heap_timer* [capacity];
        // 申请内存失败，抛出异常
        if ( ! array )
        {
            throw std::exception();
        }
        // 将每个堆元素设置为空
        for( int i = 0; i < capacity; ++i )
        {
            array[i] = NULL;
        }
        if ( size != 0 )
        {
            // 初始化堆数组
            for ( int i =  0; i < size; ++i )
            {
                array[ i ] = init_array[ i ];
            }
            // 对数组中第[cur_size-1/2 ~ 0]个元素执行下沉操作
            for ( int i = (cur_size-1)/2; i >=0; --i )
            {
                percolate_down( i );
            }
        }
    }

    // 销毁事件堆
    ~time_heap()
    {
        // 释放每个堆元素的内存
        for ( int i =  0; i < cur_size; ++i )
        {
            delete array[i];
        }
        delete [] array; 
    }

public:
    // 添加目标定时器 timer，时间复杂度为O(lgn)
    void add_timer( heap_timer* timer ) throw ( std::exception )
    {
        // 定时器为空，直接返回
        if( !timer ){
            return;
        }
        // 如果当前堆数组容量不够，则将其扩大一倍
        if( cur_size >= capacity ){
            resize();
        }
        // 获得当前堆中元素的个数
        int hole = cur_size++;
        int parent = 0;
        for( ; hole > 0; hole=parent )
        {
            // 获得父节点下标
            parent = (hole-1)/2;
            // 目标定时器的超时时间大于等于父节点的超时时间，表示我们可以直接把目标定时器插在 hole 位置了；否则，将父节点下移
            if ( array[parent]->expire <= timer->expire )
            {
                break;
            }
            array[hole] = array[parent];
        }
        // 找到合适的位置，然后把目标定时器插入到 hole 位置
        array[hole] = timer;
    }

    // 删除目标定时器 timer，时间复杂度为O(1)
    void del_timer( heap_timer* timer )
    {
        // 定时器为空，则直接返回
        if( !timer ){
            return;
        }
        // 仅仅将目标定时器的回调函数设置为空，即所谓的延迟销毁。这样节省真正删除该定时器造成的开销，但这样做容易使堆数组膨胀。
        timer->cb_func = NULL;
    }

    // 获得堆顶部的定时器，时间复杂度为O(1)
    heap_timer* top() const
    {
        if ( empty() )
        {
            return NULL;
        }
        return array[0];
    }

    // 删除堆顶部的定时器
    void pop_timer()
    {
        if( empty() ){
            return;
        }
        if( array[0] )
        {
            delete array[0];
            // 将原来的堆顶元素替换为堆数组中的最后一个元素
            // cur_size 表示大小，所以先--来获得最后一个元素的下标
            array[0] = array[--cur_size];
            // 对新的堆顶元素执行下沉操作
            percolate_down( 0 );
        }
    }

    // 心搏函数
    void tick()
    {
        heap_timer* tmp = array[0];
        // 循环处理堆中到期的定时器
        time_t cur = time( NULL );
        while( !empty() )
        {
            // 堆顶元素为空，则直接退出循环
            if( !tmp ){
                break;
            }
            // 如果堆顶定时器没到期，则退出循环
            if( tmp->expire > cur )
            {
                break;
            }
            // 否则就执行堆顶定时器中的任务
            if( array[0]->cb_func )
            {
                array[0]->cb_func( array[0]->user_data );
            }
            // 将堆顶元素删除，同时删除新的堆顶定时器
            pop_timer();
            tmp = array[0];
        }
    }

    bool empty() const { return cur_size == 0; }

private:
    // 最小堆的下沉操作
    void percolate_down( int hole )
    {
        heap_timer* temp = array[hole];
        int child = 0;
        for ( ; ((hole*2+1) <= (cur_size-1)); hole=child )
        {
            child = hole*2+1;// 孩子节点
            // 右孩子的超时时间比左孩子的超时时间短，然后移到右孩子节点上
            if ( (child < (cur_size-1)) && (array[child+1]->expire < array[child]->expire ) ){
                ++child;
            }
            // temp的超时时间大于孩子节点的超时时间，孩子节点填充到 hole 上
            if ( array[child]->expire < temp->expire ){
                array[hole] = array[child];
            }
            // temp的超时时间小于等于孩子节点的超时时间，则可以将 temp 填充到 hole 上，这样满足小根堆了
            else{
                break;
            }
        }
        array[hole] = temp;
    }

    // 将堆数组容量扩大1倍
    void resize() throw ( std::exception )
    {
        // 申请两倍容量的内存
        heap_timer** temp = new heap_timer* [2*capacity];
        // 将每个堆元素设置为空
        for( int i = 0; i < 2*capacity; ++i ){
            temp[i] = NULL;
        }
        // 内存申请失败，抛出异常
        if ( ! temp ){
            throw std::exception();
        }
        // 扩容
        capacity = 2*capacity;
        // 将堆元素进行拷贝
        for ( int i = 0; i < cur_size; ++i ){
            temp[i] = array[i];
        }
        // 删除原数组
        delete [] array;
        // 更新原数组
        array = temp;
    }

private:
    heap_timer** array;     // 堆数组
    int capacity;           // 堆数组的容量
    int cur_size;           // 堆数组当前包含元素的个数
};

#endif