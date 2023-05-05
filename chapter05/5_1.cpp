#include <stdio.h>
void byteoreder()
{
    union
    {
        short value;
        char union_bytes[sizeof(short)];
    }test;

    test.value=0x0102;
    // 大端是高位字节在前，后位字节在后：12
    if(test.union_bytes[0]==1 and test.union_bytes[1]==2){
        printf("big endian\n");
    }
    // 小端是低位字节在前，高位字节再后：21
    else if(test.union_bytes[0]==2 and test.union_bytes[1]==1){
        printf("little endian\n");
    }
    else{
        printf("unkown...\n");
    }
}

int main()
{
    // 打印结果为小端
    // 现代 PC 大多数采用小端字节序，因此小端字节序又被称为主机字节序。
    // 大端字节序也被称为网络字节序，它给所有接收数据的主机提供了一个正确解释收到的格式化数据的保证。
    byteoreder();
    return 0;
}