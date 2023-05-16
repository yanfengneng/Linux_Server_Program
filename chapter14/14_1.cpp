#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

int a = 0;
int b = 0;
pthread_mutex_t mutex_a;    // 互斥锁 a
pthread_mutex_t mutex_b;    // 互斥锁 b

void* another( void* arg )
{
    // 给信号量 b 加锁，P 操作
    pthread_mutex_lock( &mutex_b );
    printf( "in child thread, got mutex b, waiting for mutex a\n" );
    sleep( 5 );
    ++b;
    // 给信号量 a 加锁，P 操作
    pthread_mutex_lock( &mutex_a );
    b += a++;
    // 给信号量 a 解锁，V 操作
    pthread_mutex_unlock( &mutex_a );
    // 给信号量 b 解锁，V 操作
    pthread_mutex_unlock( &mutex_b );
    // 退出线程
    pthread_exit( NULL );
}

int main()
{
    pthread_t id;
    
    // 初始化互斥锁 A
    pthread_mutex_init( &mutex_a, NULL );
    // 初始化互斥锁 B
    pthread_mutex_init( &mutex_b, NULL );
    // 创建一个标识符为 id 的新线程
    pthread_create( &id, NULL, another, NULL );
    
    // 给互斥锁 A 加锁，P 操作
    pthread_mutex_lock( &mutex_a );
    printf( "in parent thread, got mutex a, waiting for mutex b\n" );
    sleep( 5 );
    ++a;
    // 给互斥锁 B 加锁，P 操作
    pthread_mutex_lock( &mutex_b );
    a += b++;
    // 给互斥锁 B 解锁，V 操作
    pthread_mutex_unlock( &mutex_b );
    // 给互斥锁 A 解锁，V 操作
    pthread_mutex_unlock( &mutex_a );

    // 等待标识符为 id 的线程结束
    pthread_join( id, NULL );
    // 销毁互斥锁 A
    pthread_mutex_destroy( &mutex_a );
    // 销毁互斥锁 B
    pthread_mutex_destroy( &mutex_b );
    return 0;
}