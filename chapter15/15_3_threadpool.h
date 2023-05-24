#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "chapter14/14_2_locker.h"

/* 线程池类：将它定义为模板类是为了代码复用。模板参数 T 是任务类。 */
template< typename T >
class threadpool
{
public:
    /* 参数 thread_number 是线程池中线程的数量，max_requests 是请求队列中最多允许、等待处理的请求的数量 */ 
    threadpool( int thread_number = 8, int max_requests = 10000 );
    ~threadpool();
    /* 往请求队列中添加任务 */
    bool append( T* request );

private:
    /* 工作线程允许的函数，它不断从工作队列中取出任务并执行之 */
    static void* worker( void* arg );
    void run();

private:
    // 线程池中的线程数
    int m_thread_number;
    // 请求队列中允许的最大请求数
    int m_max_requests;
    // 描述线程池的数据，其大小为 m_thread_number
    pthread_t* m_threads;
    // 请求队列
    std::list< T* > m_workqueue;
    // 保护请求队列的互斥锁
    locker m_queuelocker;
    // 是否有任务需要处理
    sem m_queuestat;
    // 是否结束线程
    bool m_stop;
};

template< typename T >
threadpool< T >::threadpool( int thread_number, int max_requests ) : 
        m_thread_number( thread_number ), m_max_requests( max_requests ), m_stop( false ), m_threads( NULL )
{
    if( ( thread_number <= 0 ) || ( max_requests <= 0 ) )
    {
        throw std::exception();
    }

    // 申请 m_thread_number 个子线程空间
    m_threads = new pthread_t[ m_thread_number ];
    if( ! m_threads )
    {
        throw std::exception();
    }

    /* 创建 thread_number 个线程，并将它们都设置为脱离线程 */
    for ( int i = 0; i < thread_number; ++i )
    {
        printf( "create the %dth thread\n", i );
        /* C++ 中使用 pthread_create 函数式，第三个参数必须指向一个静态函数，而要在一个静态函数中使用类的动态成员有两种方法： */
        /* 1）通过类的静态对象来调用。比如在单体模式中，静态函数通过类的全局唯一实例来访问动态成员函数。 */
        /* 2）将类的对象作为参数传递给该静态函数，然后在静态函数中引用这个对象，并调用其动态方法。下面就是将线程参数设置为 this 指针，
        然后在 worker 函数中获取该指针并调用其动态方法 run。 */
        if( pthread_create( m_threads + i, NULL, worker, this ) != 0 )
        {
            delete [] m_threads;
            throw std::exception();
        }
        if( pthread_detach( m_threads[i] ) )
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

// 线程池的析构函数
template< typename T >
threadpool< T >::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

/* 往请求队列中添加任务 */
template< typename T >
bool threadpool< T >::append( T* request )
{
    /* 操作工作队列时一定要加锁，因为它被所有线程共享 */
    m_queuelocker.lock();
    if ( m_workqueue.size() > m_max_requests )
    {
        // 请求队列的大小比最大请求数还大，需要解锁，并返回添加任务失败
        m_queuelocker.unlock();
        return false;
    }
    // 添加请求
    m_workqueue.push_back( request );
    // 解锁
    m_queuelocker.unlock();
    // 信号量+1
    m_queuestat.post();
    return true;
}

/* 工作线程允许的函数，它不断从工作队列中取出任务并执行之 */
template< typename T >
void* threadpool< T >::worker( void* arg )
{
    threadpool* pool = ( threadpool* )arg;
    pool->run();
    return pool;
}

template< typename T >
void threadpool< T >::run()
{
    // 直到线程结束，循环就终止
    while ( ! m_stop )
    {
        // P 操作：申请信号量
        m_queuestat.wait();
        // 加锁
        m_queuelocker.lock();
        // 工作队列为空，就解锁，并进入下一次循环
        if ( m_workqueue.empty() )
        {
            m_queuelocker.unlock();
            continue;
        }
        // 获得工作队列的对头事件
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        // 解锁
        m_queuelocker.unlock();
        // 事件为空，就进入下一次循环
        if ( ! request )
        {
            continue;
        }
        // 处理该事件
        request->process();
    }
}

#endif
