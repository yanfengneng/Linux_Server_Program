#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

// semctl 中的 command 参数
union semun
{
     int val;                       // 用于 SETVAl 命令             
     struct semid_ds* buf;          // 用于 IPC_STAT 和 IPC_SET 命令
     unsigned short int* array;     // 用于 GETALL 和 SETALL 命令
     struct seminfo* __buf;         // 用于 IPC_INFO 命令    
};

/* op 为 -1 时执行 P 操作，op 为 1 时执行 V 操作。 */
void pv( int sem_id, int op )
{
    struct sembuf sem_b;
    sem_b.sem_num = 0;          // 信号量的编号设置为 0，表示为信号量集中的第一个信号量。
    sem_b.sem_op = op;          // 指定操作类型。
    sem_b.sem_flg = SEM_UNDO;   // SEM_UNDO 表示当进程退出时取消正在进行的 semop 操作。
    semop( sem_id, &sem_b, 1 ); // 对 sem_id 指定的目标信号量集进行一个操作
}

int main( int argc, char* argv[] )
{
    // 给 semget 传递 IPC_PRIVATE（其值为0），这样无论该信号量是否已经存在，semget 都将创建一个新的信号量。可以在父、子进程之间实现信号量同步。
    int sem_id = semget( IPC_PRIVATE, 1, 0666 );

    union semun sem_un;
    sem_un.val = 1;
    // 对 sem_id 进行控制
    semctl( sem_id, 0, SETVAL, sem_un );

    // 创建子进程
    pid_t id = fork();
    // 创建失败
    if( id < 0 ){
        return 1;
    }
    // 本进程是子进程
    else if( id == 0 )
    {
        printf( "child try to get binary sem\n" );
        // 在父、子进程间共享 IPC_PRIVATE 信号量的关键就在于二者都可以操作该信号量的标识符 sem_id
        // 申请资源
        pv( sem_id, -1 );
        printf( "child get the sem and would release it after 5 seconds\n" );
        sleep( 5 );
        // 释放资源
        pv( sem_id, 1 );
        exit( 0 );
    }
    // 本进程是父进程
    else
    {
        printf( "parent try to get binary sem\n" );
        // 申请资源
        pv( sem_id, -1 );
        printf( "parent get the sem and would release it after 5 seconds\n" );
        sleep( 5 );
        // 释放资源
        pv( sem_id, 1 );
    }

    waitpid( id, NULL, 0 );
    semctl( sem_id, 0, IPC_RMID, sem_un );
    return 0;
}