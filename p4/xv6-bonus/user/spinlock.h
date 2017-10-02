#ifndef SPINLOCK_H
#define SPINLOCK_H

struct spinlock {
    unsigned int lock;
};

void spin_init(struct spinlock* lk);
void spin_lock(struct spinlock *lk);
void spin_unlock(struct spinlock *lk);

#endif // SPINLOCK_H
