#ifndef MUTEX_H
#define MUTEX_H

struct mutex_queue {
    int pid;
    struct mutex_queue *next;
};

struct mutex {
    int locked;
    unsigned int spinlock;
    struct mutex_queue *queue;
};

void mutex_init(struct mutex* mtx);
void mutex_lock(struct mutex* mtx);
void mutex_unlock(struct mutex* mtx);

#endif
