
#include "types.h"
#include "user.h"
void cv_init(struct condvar* cv)
{
    mutex_init(&(cv->guard));
    cv->queue = NULL;
}

static void enqueue(struct condvar *cv, int pid) {
    struct condvar_queue *q = malloc(sizeof(*q));
    q->next = cv->queue;
    q->pid = pid;
    cv->queue = q;
}

static int dequeue(struct condvar *cv) {
    struct condvar_queue *q = cv->queue;
    int pid = q->pid;
    cv->queue = q->next;
    free(q);
    return pid;
}

void cv_wait(struct condvar* cv, struct mutex* mtx)
{
    mutex_lock(&cv->guard);
    enqueue(cv, getpid());
    setpark();
    mutex_unlock(mtx);
    mutex_unlock(&cv->guard);
    park();
    mutex_lock(mtx);
}

void cv_signal(struct condvar* cv)
{
    mutex_lock(&cv->guard);
    if (cv->queue != NULL) {
        unpark(dequeue(cv));
    }
    mutex_unlock(&cv->guard);
}

void cv_broadcast(struct condvar* cv)
{
    mutex_lock(&cv->guard);
    struct condvar_queue *tmp, *q;
    q = cv->queue;
    while (q != NULL) {
        unpark(q->pid);
        tmp = q->next;
        free(q);
        q = tmp;
    }
    cv->queue = NULL;
    mutex_unlock(&cv->guard);
}
