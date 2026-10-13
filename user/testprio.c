#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

// Task 2 test: checks getpriority()/setpriority(), range checking,
// and that a child inherits its parent's priority. Then runs ps so
// the priorities can be seen in the process table.
int
main(int argc, char *argv[])
{
  int pid;

  printf("testprio: starting priority = %d\n", getpriority());

  printf("testprio: setpriority(20) returned %d (old priority)\n",
         setpriority(20));
  printf("testprio: priority now = %d\n", getpriority());

  printf("testprio: setpriority(60) returned %d (should be -1)\n",
         setpriority(60));
  printf("testprio: setpriority(-1) returned %d (should be -1)\n",
         setpriority(-1));
  printf("testprio: priority still = %d\n", getpriority());

  pid = fork();
  if (pid == 0) {
    // Child: should have inherited 20, then lowers itself to 7.
    printf("testprio child: inherited priority = %d\n", getpriority());
    setpriority(7);
    printf("testprio child: set own priority to %d\n", getpriority());
    pause(5);          // stay alive long enough to show up in ps
    exit(0);
  }

  pause(1);            // let the child print first
  pid = fork();
  if (pid == 0) {
    char *args[] = { "ps", 0 };
    exec("ps", args);
    exit(1);
  }
  wait(0);
  wait(0);
  exit(0);
}
