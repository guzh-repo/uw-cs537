#include "types.h"
#include "user.h"

int thread_create(void (*fn)(void*), void* arg) {
    void* mem = malloc(4096); // PGSIZE hardcoded
    if (mem <= 0) return -1;
    //printf(1, "thread_create: stack_addr=0x%x\n", mem);
    return clone(fn, arg, mem);
}

int thread_join(void) {
    void* mem;
    int pid = join(&mem);
    if (pid <= 0) return -1;
    //printf(1, "thread_join: stack_addr=0x%x\n", mem);
    free(mem);
    return pid;
}
