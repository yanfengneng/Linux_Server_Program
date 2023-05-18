#include<stdio.h>
#include<unistd.h>
#include<pthread.h>
#include<signal.h>
#include<errno.h>
 
void* thread_fun(void* arg)
{
    printf("i am new thread.\n");
}
 
int main()
{
    pthread_t tid;
    int err;
    int res_kill;
    
    // 创建子线程
    err = pthread_create(&tid, NULL, thread_fun, NULL);
    // 子线程创建失败
    if(err != 0)
    {   
        printf("new thread create is failed.\n");
        return 0;
    }   
    sleep(1);
    // 用来检测子线程是否存在。给子线程 tid 发送键盘输入（键盘输入使进程退出 Ctrl+\ ）信号。
    // 但是子线程没有这种信号，因此使用如下这个函数来检测目标子线程是否存在
    res_kill = pthread_kill(tid, SIGQUIT);
    if(res_kill == ESRCH)// 目标线程不存在
    {   
        printf("new thread tid is not found.\n");
        printf("ret_kill = %d\n",res_kill);
    }
    // 关闭子线程
    int thread_join = pthread_join(tid, NULL);
    printf("i am main thread .\n");
    return 0;
}