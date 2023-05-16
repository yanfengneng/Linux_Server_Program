#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

int a = 0;
int b = 0;
pthread_mutex_t mutex_a;    // 互斥锁 a
pthread_mutex_t mutex_b;    // 互斥锁 b

void* another( void* arg )
{
    // 给信号量 b 加锁，P 操作。子线程获得 B 的锁了。
    pthread_mutex_lock( &mutex_b );
    printf( "in child thread, got mutex b, waiting for mutex a\n" );
    sleep( 5 );
    ++b;
    // 由于主线程已经给 a 加锁了，这里我们在加锁就造成死锁了！
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
    
    // 给互斥锁 A 加锁，P 操作。主线程获得 A 的锁了
    pthread_mutex_lock( &mutex_a );
    printf( "in parent thread, got mutex a, waiting for mutex b\n" );
    /* 这里加入 sleep 来模拟连续两次调用 pthread_mutex_lock 之间的时间差，这样下来主线程先占有 mutex_a，子线程占有 mutex_b。
    然后子线程等待 mutex_a，主线程等待 mutex_b，这样主线程和子线程相互等待对方的资源就造成死锁了。 */
    sleep( 5 );
    ++a;
    // 由于子线程已经获得 B 的锁了，但是没有释放 B 的锁，因此我们在主线程中在申请 B 的锁，必定会造成死锁了。
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
    // 由于造成死锁了，所以程序不会执行到这里。这条语句不会被打印。
    printf("over...");
    return 0;
}