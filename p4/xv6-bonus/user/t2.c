#include "types.h"
#include "stat.h"
#include "user.h"

struct spinlock lock;
int x = 0;

void func(void *arg) {
    for (int i = 0;i<10;i++) {
        spin_lock(&lock);
        int tmp = x;
        tmp = tmp+1;
        x = tmp;
        spin_unlock(&lock);
    }
    exit();
}

void func2(void *arg) {
    for (int i = 0;i<10;i++) {
        int tmp = x;
        sleep(1);
        tmp = tmp+1;
        x = tmp;
    }
    exit();
}

int main() {
    spin_init(&lock);
    for (int i=0;i<20;i++) thread_create(func, NULL);
    for (int i=0;i<20;i++) thread_join();
    printf(1, "with lock x=%d\n",x);
    x=0;
    for (int i=0;i<20;i++) thread_create(func2, NULL);
    for (int i=0;i<20;i++) thread_join();
    printf(1, "no lock x=%d\n",x);

    exit();
}
