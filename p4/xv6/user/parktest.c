#include "types.h"
#include "stat.h"
#include "user.h"

void func(void *arg) {
    setpark();
    printf(1, "Thread setpark()\n");
    sleep(100);
    printf(1, "Thread parking...\n");
    park();
    printf(1, "Thread unparked...\n");
    exit();
}

int main1() {
    int p1 = thread_create(func, NULL);
    //printf(1, "thread_created, parent sleeping\n");
    sleep(50);
    printf(1, "Parent awake, unpark\n");
    unpark(p1);
    int p2 = thread_join();
    printf(1, "Parent after\n");
    printf(1, "p1=%d p2=%d\n", p1, p2);
    exit();
}

void func2(void *arg) {
    printf(1, "Thread parking...\n");
    park();
    printf(1, "Thread unparked...\n");
    exit();
}

int main2() {
    int p1 = thread_create(func2, NULL);
    sleep(150);
    printf(1, "Parent awake, unpark\n");
    unpark(p1);
    int p2 = thread_join();
    printf(1, "Parent after\n");
    printf(1, "p1=%d p2=%d\n", p1, p2);
    exit();
}

int main() {
    return main1();
}
