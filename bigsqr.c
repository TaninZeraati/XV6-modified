#include "types.h"
#include "user.h"
#include "fcntl.h"

int main(int argc,char *argv[])
{
    int number = atoi(argv[1]);
    int backup;

    printf(1,"The number is %d\n",number);

    __asm__("movl %%edx, %0" : "=r" (backup));
    __asm__("movl %0, %%edx" : :"r" (number));
    printf(1,"The biggest perfest square is : %d\n", calc_perfect_square());
    __asm__("movl $22, %eax");
    __asm__("int $64");
    __asm__("movl %0, %%edx" : : "r" (backup));
    exit();
}