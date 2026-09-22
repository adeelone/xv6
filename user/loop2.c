#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pid = getpid();
  printf("loop2: my pid is %d\n", pid);

  for (int i = 0; i < 2000000; i++) {
    if (i % 400000 == 0)
      printf("loop2 (pid %d): i=%d\n", pid, i);
  }

  printf("loop2 (pid %d): done\n", pid);
  exit(0);
}
