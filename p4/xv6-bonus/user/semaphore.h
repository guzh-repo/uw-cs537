#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "mutex.h"
#include "condvar.h"

struct semaphore {
    struct mutex guard;
    struct condvar queue;
    int counter;
};

void sem_init(struct semaphore* sem, int initval);
void sem_post(struct semaphore* sem);
void sem_wait(struct semaphore* sem);

#endif
