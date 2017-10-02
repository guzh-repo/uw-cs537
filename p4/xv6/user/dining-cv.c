#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM 2
#define LOP 100

int forks[NUM];

struct condvar cv;
struct mutex table_lock;
struct mutex print_lock;

struct table_info {
    int *left, *right;
    int idx, seed;
};

int nextInt(int max, int *seed) {
    *seed = (8253729 * (*seed) + 2396403);
    if (*seed < 0)  *seed=-*seed;
    return *seed%max;
}

void philosopher(void *arg) {
    struct table_info *info = arg;
    int i;
    for (i=0; i<LOP; i++) {
        int wait_time = nextInt(3, &info->seed)+1;
        int eat_time  = nextInt(6, &info->seed)+1;

        mutex_lock(&print_lock);
        printf(1, "%d   %d now thinking... think_time: %d, eating_time: %d\n",i, info->idx, wait_time, eat_time);
        mutex_unlock(&print_lock);

        sleep(wait_time);

        mutex_lock(&print_lock);
        //printf(1, "%d now picking forks...\n", info->idx);
        mutex_unlock(&print_lock);

        mutex_lock(&table_lock);
        while (*info->left!=0 || *info->right!=0) cv_wait(&cv, &table_lock);
        *info->left = 1;
        *info->right= 1;
        mutex_unlock(&table_lock);

        mutex_lock(&print_lock);
        //printf(1, "%d now eating...\n", info->idx);
        mutex_unlock(&print_lock);

        sleep(eat_time);

        mutex_lock(&print_lock);
        //printf(1, "%d finished eating...\n", info->idx);
        mutex_unlock(&print_lock);

        mutex_lock(&table_lock);
        *info->left = 0;
        *info->right= 0;
        cv_broadcast(&cv);
        mutex_unlock(&table_lock);

        mutex_lock(&print_lock);
        //printf(1, "%d put forks back...\n", info->idx);
        mutex_unlock(&print_lock);
    }
    exit();
}

int main(int argc, char *argv[]) {
    cv_init(&cv);
    mutex_init(&print_lock);
    mutex_init(&table_lock);
    int seed = atoi(argv[1]);
    int i;
    printf(1, "seed = %d\n", seed);
    for (i=0;i<NUM;i++) {
        forks[i] = 0;
    }
    for (i=0;i<NUM;i++) {
        struct table_info *info = malloc(sizeof(*info));
        info->left = forks+i;
        info->right = forks+i+1;
        if (i==NUM-1) info->right = forks;
        info->idx=i;
        info->seed=seed;
        thread_create(philosopher, info);
    }
    for (i=0;i<NUM;i++) {
        thread_join();
    }
    exit();
}
