#include "types.h"
#include "stat.h"
#include "user.h"

struct spinlock lock;

void func(void *arg) {
    register unsigned int ebp asm("%ebp");
    spin_lock(&lock);
    printf(1, "Thread: %d EBP=%x\n", *(int*)arg, ebp);
    spin_unlock(&lock);
    exit();
}

int main() {
    P("t1 start\n");
    int x = 123456;
    printf(1, "t1 lock: %x\n", &lock);
    spin_init(&lock);
    spin_lock(&lock);
    printf(1, "Parent before\n");
    int p1 = thread_create(func, &x);
    printf(1, "thread_created\n");
    sleep(100);
    printf(1, "Parent awake\n");
    spin_unlock(&lock);
    int p2 = thread_join();
    printf(1, "Parent after\n");
    printf(1, "p1=%d p2=%d\n", p1, p2);
    exit();
}
