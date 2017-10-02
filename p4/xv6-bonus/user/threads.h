#ifndef THREADS_H
#define THREADS_H

/* This file shouldn't require any modifications. */

int thread_create(void (*thfunc)(void*), void* arg);
int thread_join(void);

#endif
