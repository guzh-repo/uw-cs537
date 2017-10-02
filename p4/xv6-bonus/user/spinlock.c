#include "types.h"
#include "user.h"
#include "x86.h"
#include "spinlock.h"

void spin_init(struct spinlock* lk) {
    lk->lock = 0;
}

void spin_lock(struct spinlock* lk) {
    //printf(1, "wait lock...%x\n", lk);
    while(xchg(&lk->lock, 1) != 0);
    //printf(1, "acquired %x!\n", lk);
}

void spin_unlock(struct spinlock* lk) {
    //printf(1, "unlock...%x\n", lk);
    lk->lock = 0;
}
