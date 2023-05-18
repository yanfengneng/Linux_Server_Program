#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <wait.h>

pthread_mutex_t mutex;

/* 子线程运行的函数：它首先获得互斥锁 mutex，然后暂停 5s，再释放该互斥锁。 */
void* another( void* arg )
{
    printf( "in child thread, lock the mutex\n" );
    pthread_mutex_lock( &mutex );
    sleep( 5 );
    pthread_mutex_unlock( &mutex );
}

void prepare()
{
    pthread_mutex_lock( &mutex );
}

void infork()
{
    pthread_mutex_unlock( &mutex );
}

int main()
{
    // 初始化互斥锁
    pthread_mutex_init( &mutex, NULL );
    pthread_t id;
    // 创建一个子线程
    pthread_create( &id, NULL, another, NULL );
    /* 父进程中的主线程暂停 1s，以确保在执行 fork 操作之前，子线程已经开始运行并获得了互斥变量 mutex。 */
    sleep( 1 );
    // 在父进程中创建一个子进程
    int pid = fork();
    if( pid < 0 )// 子进程创建失败，则销毁父进程中的子线程与互斥信号量
    {
        pthread_join( id, NULL );// 回收子线程
        pthread_mutex_destroy( &mutex );// 销毁互斥信号量
        return 1;
    }
    else if( pid == 0 )// pid=0，表是子进程，也就是子进程创建成功
    {
        printf( "I anm in the child, want to get the lock\n" );
        /* 子进程从父进程继承了 mutex 的状态，该互斥锁处于锁住的状态，这是由父进程中的子线程执行 pthread_mutex_lock 引起的。
        因此，下面这句加锁操作会一直阻塞，尽管从逻辑上来说它是不应该阻塞的。 */
        pthread_mutex_lock( &mutex );// 阻塞
        printf( "I can not run to here, oop...\n" );
        pthread_mutex_unlock( &mutex );
        exit( 0 );
    }
    else// 在父进程中解锁并释放资源
    {
        pthread_mutex_unlock( &mutex );
        wait( NULL );
    }
    pthread_join( id, NULL );// 回收子线程
    pthread_mutex_destroy( &mutex );// 销毁互斥信号量
    return 0;
}