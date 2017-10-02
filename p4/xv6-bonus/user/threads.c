#include "types.h"
#include "user.h"
#include "syscall.h"
#include "traps.h"

int thread_create(void (*fn)(void*), void* arg) {
    void* mem = malloc(4096); // PGSIZE hardcoded
    if (mem <= 0) return -1;
    *(void**)(mem+4096-4) = arg;
    *(unsigned int*)(mem+4096-8) = 0xffffffff; // fake return
    //printf(1, "mem:0x%x arg_addr:0x%x fn_addr:0x%x\n", mem, mem+4096-4, mem+4096-12);

    int pid;
    asm volatile(
        "  movl %2, %%ecx    \n" // %ecx will be restored after syscall
        "  pushl %1          \n" // push the actual syscall parameter
        "  pushl $0          \n" // push some junk to make argptr() happy
        "  movl $22, %%eax   \n" // 22 == SYS_clone
        "  int $64           \n" // 64 == T_SYSCALL

        "  test %%eax, %%eax \n"
        "  jne is_parent     \n" // jump to parent if return value isn't zero
        "is_child:           \n"
        "  pushl %%ecx       \n" // %esp changed to child stack
        "  ret               \n" // "ret" to child function
        "is_parent:          \n" // %esp in parent stack
        "  addl $8, %%esp    \n" // balance stack
        "  movl %%eax, %0      " // child pid
        :"=r"(pid)
        :"r"(mem), "r"(fn)
        :"eax", "ecx"
    );
    return pid;
}

int thread_join(void) {
    void* mem;
    int pid = join(&mem);
    if (pid <= 0) return -1;
    //printf(1, "thread_join: stack_addr=0x%x\n", mem);
    free(mem);
    return pid;
}
