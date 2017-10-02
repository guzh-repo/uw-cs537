#ifndef CONDVAR_H
#define CONDVAR_H

#include "condvar.h"

struct condvar_queue {
    int pid;
    struct condvar_queue *next;
};

struct condvar {
    struct mutex guard;
    struct condvar_queue *queue;
};

void cv_init(struct condvar* cv);
void cv_wait(struct condvar* cv, struct mutex* mtx);
void cv_signal(struct condvar* cv);
void cv_broadcast(struct condvar* cv);

#endif
