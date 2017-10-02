#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "At 0x0: %d\n", *(int*)0);
  exit();
}
