#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"

/* Check basic functionality for part 2: should exit cleanly with no output. */

int
main(int argc, char** argv)
{
	int x[2];
	if ((char*)&x < (char*)(USERTOP - 4096))
		printf(1, "oops [4]\n");
	if (pipe(x) < 0)
		printf(1, "oops [5]\n");
	exit();
}
