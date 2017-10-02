#include "types.h"
#include "user.h"

void sem_init(struct semaphore* sem, int initval)
{
    sem->counter = initval;
    cv_init(&sem->queue);
    mutex_init(&sem->guard);
}

void sem_wait(struct semaphore* sem)
{
    mutex_lock(&sem->guard);
    if (sem->counter <= 0) {
        while(sem->counter <= 0) {
            cv_wait(&sem->queue, &sem->guard);
        }
    }
    sem->counter--;
    mutex_unlock(&sem->guard);
}

void sem_post(struct semaphore* sem)
{
    mutex_lock(&sem->guard);
    cv_signal(&sem->queue);
    sem->counter++;
    mutex_unlock(&sem->guard);
}
