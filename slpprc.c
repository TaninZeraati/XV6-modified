#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "date.h" 


int main(int argc,char *argv[])
{
    int number = atoi(argv[1]);
    int backup;
    // struct rtcdate *r;

    // printf(1,"The current time is : \n");
    // cmostime(r);
    printf(1,"The process should sleep for %d\n",number);

    __asm__("movl %%edx, %0" : "=r" (backup));
    __asm__("movl %0, %%edx" : :"r" (number));
    sleep_time();
    __asm__("movl $22, %eax");
    __asm__("int $64");
    __asm__("movl %0, %%edx" : : "r" (backup));
    // printf(1,"The wake up time is : \n");

    // cmostime(r);
    exit();
}