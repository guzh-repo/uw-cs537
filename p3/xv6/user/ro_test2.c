#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  char* s = "this is a string constant";
  printf(1, "it should crash twice\n");
  fork();
  *s = 'x';
  exit();
}
