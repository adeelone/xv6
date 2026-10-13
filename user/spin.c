#include "kernel/types.h"
#include "user/user.h"

// spin <units>: CPU-bound test job. One unit is roughly one tick.
// Prints how long it took when it finishes.
int
main(int argc, char *argv[])
{
  volatile int x = 0;
  int units = 20;
  int start = uptime();

  if (argc > 1)
    units = atoi(argv[1]);
  for (int u = 0; u < units; u++)
    for (int i = 0; i < 30000000; i++)
      x++;
  printf("spin pid %d (prio %d) done: %d ticks\n",
         getpid(), getpriority(), uptime() - start);
  exit(0);
}
