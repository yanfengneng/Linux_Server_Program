#include <unistd.h>
#include <stdio.h>
#include <iostream>
using namespace std;

static bool switch_to_user( uid_t user_id, gid_t gp_id )
{
    // 先确保用户不是 root
    if ( ( user_id == 0 ) && ( gp_id == 0 ) )
    {
        return false;
    }
    // 获得真实组 ID
    gid_t gid = getgid();
    // 获得用户 ID
    uid_t uid = getuid();
    if ( ( ( gid != 0 ) || ( uid != 0 ) ) && ( ( gid != gp_id ) || ( uid != user_id ) ) )
    {
        return false;
    }

    // 如果不是 root，则已经是目标用户了
    if ( uid != 0 )
    {
        return true;
    }

    // 切换到目标用户
    if ( ( setgid( gp_id ) < 0 ) || ( setuid( user_id ) < 0 ) )
    {
        return false;
    }

    return true;
}

int main()
{
    uid_t uid = getuid();
    uid_t gid = getgid();
    printf( "userid is %d, group id is: %d\n", uid, gid );
    switch_to_user(uid,gid);
    printf( "After switch: userid is %d,  group id is: %d\n", uid, gid );
    return 0;
}