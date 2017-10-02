#include "types.h"
#include "user.h"
#include "x86.h"

void mutex_init(struct mutex* mtx)
{
    mtx->locked = 0;
    mtx->spinlock = 0;
    mtx->queue = NULL;
}

void mutex_lock(struct mutex* mtx)
{
    while(xchg(&(mtx->spinlock), 1) != 0);
    if (mtx->locked) {
        struct mutex_queue *t = malloc(sizeof(*t));
        t->pid = getpid();
        t->next = mtx->queue;
        mtx->queue = t;
        setpark();
        mtx->spinlock = 0;
        park();
    } else {
        mtx->locked = 1;
        mtx->spinlock = 0;
    }
}

void mutex_unlock(struct mutex* mtx)
{
    while(xchg(&(mtx->spinlock), 1) != 0);
    if (mtx->queue) {
        struct mutex_queue *p = mtx->queue;
        int nxt = p->pid;
        mtx->queue = p->next;
        free(p);
        unpark(nxt);
    } else {
        mtx->locked = 0;
    }
    mtx->spinlock = 0;
}
