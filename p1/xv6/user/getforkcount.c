#include "types.h"
#include "user.h"

int main(int argc, char *argv[]) {
    printf(2, "%d\n", getforkcount());
    exit();
}
