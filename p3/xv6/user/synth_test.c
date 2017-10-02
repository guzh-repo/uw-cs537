#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

#define PGSIZE 4096
#define assert(x) ((x)?("PASS"):("FAIL"))

int ttt;

int main(int argc, char *argv[]) {
    int stub, x;
    printf(1, "==Null Dereference Tests==\n");
    int i;
    for (i = 0; i <4;i++) {
        printf(1, "CRASH: ");
        if (fork() == 0) {
            printf(1, "%x\n", *(int *)i);
            exit();
        } else {
            wait();
        }
        printf(1, "CRASH: ");
        if (fork() == 0) {
            printf(1, "%x\n", *(int *)(PGSIZE-i-1));
            exit();
        } else {
            wait();
        }
    }
    printf(1, "NORML: 0x%x\n", *(int*)PGSIZE);

    printf(1, "==High address stack==\n");
    printf(1, "%s: local var addr > brk\n", assert((uint)&stub > (uint)sbrk(0)));
    printf(1, "%s: local var in last frame\n", assert((uint)&stub > USERTOP-PGSIZE));

    printf(1, "==Stack growth test==\n");
    if (fork() == 0) {
        printf(1, "INFO: sbrk(0)=0x%x\n", (uint)sbrk(0));
        int lastaddr = USERTOP;
        while(lastaddr > (uint)sbrk(0)) lastaddr -= PGSIZE;
        lastaddr += PGSIZE*2;
        printf(1, "INFO: lastaddr=0x%x\n", lastaddr);
        int i;
        for (i = USERTOP-PGSIZE; i >= lastaddr; i-=1) {
            //printf(1, "INFO: access=0x%x\n", i);
            x = *(int*)i;
        }
        printf(1, "INFO: last visited=0x%x\n", i+1);
        printf(1, "PASS: auto growth\n");
        printf(1, "CRASH: ");
        x = *(int*)(i);
        exit();
    }
    wait();

    if (fork() == 0) {
        printf(1, "%s: sbrk() shouldn't fail\n", assert((int)sbrk(2*PGSIZE)>0));
        printf(1, "INFO: sbrk(0)=0x%x\n", (uint)sbrk(0));
        int lastaddr = USERTOP;
        while(lastaddr > (uint)sbrk(0)) lastaddr -= PGSIZE;
        lastaddr += PGSIZE*2;
        printf(1, "INFO: lastaddr=0x%x\n", lastaddr);
        int i;
        for (i = USERTOP-PGSIZE; i >= lastaddr; i-=1) {
            //printf(1, "INFO: access=0x%x\n", i);
            x = *(int*)i;
        }
        printf(1, "INFO: last visited=0x%x\n", i+1);
        printf(1, "PASS: auto growth\n");
        printf(1, "%s: sbrk() should fail\n", assert((int)sbrk(2*PGSIZE)==-1));
        exit();
    }
    wait();

    printf(1, "CRASH: ");
    if (fork() == 0) {
        x = *(int*)(USERTOP-5*PGSIZE);
        exit();
    }
    wait();

    printf(1, "==Read only test==\n");
    if (fork() == 0) {
        printf(1, "CRASH: ");
        *(char*)&main = 'p';
        exit();
    }
    wait();
    if (fork() == 0) {
        printf(1, "CRASH: ");
        char* str = "test";
        str[0] = 'p';
        exit();
    }
    wait();

    ttt = 42;
    x=42;
    x = (int)sbrk(2*PGSIZE);
    *(char*)x = 'p';
    *(char*)(x+PGSIZE) = 'p';
    printf(1, "PASS: static RW data\n");

    if (fork() == 0) {
        int lastaddr = USERTOP;
        while(lastaddr > (uint)sbrk(0)) lastaddr -= PGSIZE;
        lastaddr += PGSIZE*2;
        int i;
        for (i = USERTOP-PGSIZE; i >= lastaddr; i-=1) {
            *(int*)i = 42;
        }
        printf(1, "PASS: stack grow RW\n");
        exit();
    }
    wait();

    (void)x;
    exit();
}
