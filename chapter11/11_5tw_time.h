#ifndef TIME_WHEEL_TIMER
#define TIME_WHEEL_TIMER

#include <time.h>
#include <netinet/in.h>
#include <stdio.h>

#define BUFFER_SIZE 64
class tw_timer;

/* 绑定 socket 和定时器 */
struct client_data
{
    sockaddr_in address;        // 客户端 socket 地址
    int sockfd;                 // socket 文件描述符
    char buf[ BUFFER_SIZE ];    // 读缓存
    tw_timer* timer;            // 定时器
};

/* 定时器类 */
class tw_timer
{
public:
    // 构造函数
    tw_timer( int rot, int ts ) 
    : next( NULL ), prev( NULL ), rotation( rot ), time_slot( ts ){}

public:
    int rotation;                       // 记录定时器在时间轮转多少圈后生效
    int time_slot;                      // 记录定时器属于时间轮上哪个槽（对应的链表）
    void (*cb_func)( client_data* );    // 定时器回调函数
    client_data* user_data;             // 客户数据
    tw_timer* next;                     // 指向下一个定时器
    tw_timer* prev;                     // 指向前一个定时器
};

/* 时间轮类 */
class time_wheel
{
public:
    time_wheel() : cur_slot( 0 )
    {
        // 初始化每个槽上链表的头节点
        for( int i = 0; i < N; ++i ){
            slots[i] = NULL;
        }
    }

    // 析构函数
    ~time_wheel()
    {
        // 遍历每个槽，并销毁其中的定时器
        for( int i = 0; i < N; ++i )
        {
            tw_timer* tmp = slots[i];
            // 删除链表中每个节点
            while( tmp )
            {
                slots[i] = tmp->next;
                delete tmp;
                tmp = slots[i];
            }
        }
    }

    /* 根据定时值 timeout 创建一个定时器，把它插入合适的槽中 */
    tw_timer* add_timer( int timeout )
    {
        // 定时时间非法，直接返回空
        if( timeout < 0 ){
            return NULL;
        }
        int ticks = 0;
        /* 下面根据待插入定时器的超时值计算它将在时间轮转动多少个滴答后被触发，并将该滴答数存储在变量 ticks 中。
        如果待插入定时器的超时值小于时间轮的槽间隔 TI，则将 ticks 向上折合为 1，否则就将 ticks 向下折合为 timeout/TI */
        if( timeout < TI )
        {
            ticks = 1;// 所在槽数设置为 1
        }
        else
        {
            ticks = timeout / TI; // 所在槽数为 timeout/TI
        }
        // 计算待插入的定时器在时间轮转动多少圈后被触发
        int rotation = ticks / N;
        // 计算待插入的定时器应该被插入哪个槽中。ticks%N 表示需要走 ticks%N 个槽，然后加上现在所在的槽数cur_slot，最后对相加的结果取模为最终需要查到的槽上
        int ts = ( cur_slot + ( ticks % N ) ) % N;
        // 创建新的定时器，它在时间轮上转动 rotation 圈之后被触发，且位于第 ts 个槽上
        tw_timer* timer = new tw_timer( rotation, ts );
        // 如果第 ts 个槽尚无任何定时器，则把新建的定时器插入其中，并将该定时器设置为该槽的头节点
        if( !slots[ts] )// 该槽为空，则直接将该定时值作为该槽链表的头节点
        {
            printf( "add timer, rotation is %d, ts is %d, cur_slot is %d\n", rotation, ts, cur_slot );
            slots[ts] = timer;
        }
        // 否则将定时器插入到第 ts 个槽中。采用头插法来节约插入时间
        else
        {
            timer->next = slots[ts];
            slots[ts]->prev = timer;
            slots[ts] = timer;
        }
        return timer;
    }

    // 删除目标定时器 timer
    void del_timer( tw_timer* timer )
    {
        // 若定时器为空，直接返回
        if( !timer ){
            return;
        }
        // 获得定时器所在的槽
        int ts = timer->time_slot;
        // slots[ts] 是目标定时器所在槽的头节点。如果目标定时器就是该头节点，则需要重置第 ts 个槽的头节点。
        if( timer == slots[ts] )
        {
            slots[ts] = slots[ts]->next;
            if( slots[ts] ){
                slots[ts]->prev = NULL;// 断开前向连接
            }
            delete timer;
        }
        else
        {
            // 删除定时器 timer
            timer->prev->next = timer->next;
            if( timer->next ){
                timer->next->prev = timer->prev;
            }
            delete timer;
        }
    }

    // SI 时间到后，调用该函数，时间轮向前滚动一个槽的间隔
    void tick()
    {
        // 取得时间轮上当前槽的头节点
        tw_timer* tmp = slots[cur_slot];
        printf( "current slot is %d\n", cur_slot );
        while( tmp )
        {
            printf( "tick the timer once\n" );
            // 如果定时器的 rotation 值大于 0，说明定时器在时间轮上还继续生效，不删除节点
            if( tmp->rotation > 0 )
            {
                tmp->rotation--;
                tmp = tmp->next;
            }
            // 否则说明定时器已经到期了，于是执行定时任务，然后删除该定时器
            else
            {
                // 调用回调函数
                tmp->cb_func( tmp->user_data );
                if( tmp == slots[cur_slot] )// 删除该槽链表上的头节点
                {
                    printf( "delete header in cur_slot\n" );
                    slots[cur_slot] = tmp->next;
                    delete tmp;
                    if( slots[cur_slot] )
                    {
                        slots[cur_slot]->prev = NULL;
                    }
                    tmp = slots[cur_slot];
                }
                else
                {
                    // 删除该槽链表上的中间节点
                    tmp->prev->next = tmp->next;
                    if( tmp->next )
                    {
                        tmp->next->prev = tmp->prev;
                    }
                    tw_timer* tmp2 = tmp->next;
                    delete tmp;
                    tmp = tmp2;
                }
            }
        }
        // 更新时间轮的当前槽，以反映时间轮的转动
        cur_slot = ++cur_slot % N;
    }

private:
    static const int N = 60;    // 时间轮上槽的数目
    static const int TI = 1;    // 每 1s，时间轮转动一次，即槽间隔为 1s
    tw_timer* slots[N];         // 时间轮的槽，其中每个元素指向一个定时器链表，链表无序
    int cur_slot;               // 时间轮的当前槽
};

#endif