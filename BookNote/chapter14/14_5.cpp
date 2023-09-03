#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
 
#define handle_error_en(en, msg) \
do {errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)
 
static void* sig_thread(void* arg)
{
    sigset_t* set = (sigset_t*) arg;
    int s, sig;
    for(; ;)
    {
        // 第二个步骤，调用 sigwait 等待信号
        s = sigwait(set, &sig);
        if (s != 0)// 等待信号失败，则报错
        {
            handle_error_en(s, "sigwait");
        }
        // 等待信号成功
        printf("signal handling thread got signal %d\n", sig);
    }
}
 
int main()
{
    pthread_t tid;
    sigset_t set;
    int s;
 
    // 第一个步骤，在主线程中设置信号掩码
    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);   // 添加退出进程CTRL + \信号
    sigaddset(&set, SIGUSR1);   // 添加用户自定义信号
    s = pthread_sigmask(SIG_BLOCK, &set, NULL); // 往当前信号屏蔽集中加入set
    if (s !=0)
        handle_error_en(s, "pthread_sigmask");
    // 创建子线程
    s = pthread_create(&tid, NULL, &sig_thread, (void*)&set);
    if (s != 0)// 子线程创建失败，则报错
        handle_error_en(s, "pthread_create");
 
    pause();
    return 0;
}