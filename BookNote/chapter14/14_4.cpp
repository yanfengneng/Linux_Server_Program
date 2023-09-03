#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <wait.h>
 
pthread_mutex_t mutex;

/* 子线程运行的函数。它首先获得互斥锁，然后暂停 5s，再释放该互斥锁。 */ 
void* doit(void *arg)
{
    printf("pid = %d, begin doit!\n", getpid());
    pthread_mutex_lock(&mutex);
    sleep(1);
    pthread_mutex_unlock(&mutex);
    printf("pid = %d, end doit!\n", getpid());
}

void prepare()
{
    pthread_mutex_unlock(&mutex);
}

void parent()
{
    pthread_mutex_lock(&mutex);
}

int main()
{
    pthread_mutex_init(&mutex, NULL);
    /* 在执行fork() 创建子进程之前，先执行 prepare(), 将子线程加锁的 mutex 解锁下。
    然后为了与doit() 配对，在创建子进程成功后，父进程调用 parent() 再次加锁，这时父进程的 doit() 就可以接着解锁执行下去。
    而对于子进程来说，由于在fork() 创建子进程之前，mutex已经被解锁，故复制的状态也是解锁的，所以执行doit()就不会死锁了。*/
    pthread_atfork(prepare, parent, NULL);
    printf("pid = %d Enter main ...\n", getpid());
    pthread_t tid;
    // 创建子线程
    pthread_create(&tid, NULL, doit, NULL);
    sleep(1);
    pid_t pid = fork();
    if (pid < 0)
    {
        pthread_join(tid, NULL);
        pthread_mutex_destroy(&mutex);
        return 1;
    }
    else if (pid == 0)
    {
        doit(NULL);
    }
    else
    {
        wait(NULL);
    }
    pthread_join(tid, NULL);
    printf("pid = %d Exit main ...\n", getpid());
    pthread_mutex_destroy(&mutex);
    return 0;
}