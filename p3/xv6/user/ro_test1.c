#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "it should crash twice\n");
  fork();
  char* p = (char*)&main;
  *p = 'x';
  exit();
}
