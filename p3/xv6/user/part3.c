#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

/* Check basic functionality for part 3: should print one line and then trap & die. */

void f3(void)
{
	volatile char blob[1];
	blob[0] = 0;
	blob[0];
	printf(1, "oops [9]\n");
}

void f2(void)
{
	volatile char blob[50000];
	blob[0] = 0;
	blob[0];
	f3();
}

void f1(void)
{
	int x[2];
	if ((char*)&x > (char*)(USERTOP - 4096))
		printf(1, "...what?? (something is very wrong.) [7]\n");
	if (pipe(x) < 0)
		printf(1, "oops [8]\n");
	printf(1, "should see this\n");
	f2();
}

void f0(void)
{
	int i;
	volatile char blob[5000];
	for (i = 0; i < sizeof(blob); i++)
		blob[i] = 0;
	f1();
}

int
main(int argc, char** argv)
{
	char x;
	if (&x < (char*)(USERTOP - 4096))
		printf(1, "oops [6]\n");
	f0();
	exit();
}
