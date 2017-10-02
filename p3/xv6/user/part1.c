#include "types.h"
#include "stat.h"
#include "user.h"

/* Check basic functionality for part 1: should print one line and then trap & die. */

int
main(int argc, char** argv)
{
	int* p = NULL;
	if (pipe(p) != -1)
		printf(1, "oops [1]\n");
	if (pipe(p+1) != -1)
		printf(1, "oops [2]\n");
	printf(1, "should see this\n");
	printf(1, "oops [3]: %d\n", *p);
	exit();
}
