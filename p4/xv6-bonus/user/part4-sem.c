#include "types.h"
#include "stat.h"
#include "user.h"

/*
 * Takes one command-line argument
 *  0: fast producer, fast consumer
 *  1: slow producer, fast consumer
 *  2: fast producer, slow consumer
 *  3: slow producer, slow consumer
 *
 * Should print numbers 0..41 and then exit.
 */

#define QUEUELEN 10

static int buffer[QUEUELEN];
static int qhead, qtail;

static void insert(int value)
{
	buffer[qhead] = value;
	qhead = (qhead + 1) % QUEUELEN;
}

static int remove(void)
{
	int tmp = buffer[qtail];
	qtail = (qtail + 1) % QUEUELEN;
	return tmp;
}

struct semaphore empty, full, mutex;

static void produce(int value)
{
	sem_wait(&empty);
	sem_wait(&mutex);
	insert(value);
	sem_post(&mutex);
	sem_post(&full);
}

static int consume(void)
{
	int tmp;

	sem_wait(&full);
	sem_wait(&mutex);
	tmp = remove();
	sem_post(&mutex);
	sem_post(&empty);

	return tmp;
}

#define COUNT 42

static void producer_threadfunc(void* arg)
{
	int i;
	int mode = (int)arg;

	for (i = 0; i < COUNT; i++) {
		produce(i);
		if (mode & 1)
			sleep(i);
	}

	/* Use -1 to indicate termination */
	produce(-1);

	exit();
}

static void consumer_threadfunc(void* arg)
{
	int val;
	int mode = (int)arg;
	int expected = 0;

	for (;;) {
		val = consume();
		if (val == -1)
			break;
		if (val != expected)
			printf(1, "oops, consumer got %d, expected %d\n", val, expected);
		printf(1, "%d\n", val);
		expected += 1;
		if (mode & 2)
			sleep(val);
	}

	exit();
}

int
main(int argc, char *argv[])
{
	int ptid, ctid;
	int jtid1, jtid2;
	int mode = -1;

	if (argc == 2)
		mode = atoi(argv[1]);

	if (mode < 0 || mode > 3) {
		printf(1, "Usage: part4 {0,1,2,3}\n");
		exit();
	}

	sem_init(&empty, QUEUELEN);
	sem_init(&full, 0);
	sem_init(&mutex, 1);

	ptid = thread_create(producer_threadfunc, (void*)mode);
	ctid = thread_create(consumer_threadfunc, (void*)mode);

	if (ptid < 1 || ctid < 1) {
		printf(1, "oops, thread_create() failed\n");
		exit();
	}

	jtid1 = thread_join();
	jtid2 = thread_join();

	if (!((jtid1 == ptid && jtid2 == ctid)
	      || (jtid1 == ctid && jtid2 == ptid)))
		printf(1, "oops, thread_join() returned unexpected PIDs\n");

	exit();
}
