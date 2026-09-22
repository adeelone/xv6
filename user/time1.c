#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: time1 command [args...]\n");
    exit(1);
  }

  // Build a null-terminated argument list for exec,
  // skipping argv[0] which is "time1" itself.
  char *args[MAXARG];
  int n = 0;
  for(int i = 1; i < argc && n < MAXARG - 1; i++)
    args[n++] = argv[i];
  args[n] = 0;

  int start = uptime();

  int pid = fork();
  if(pid < 0){
    fprintf(2, "time1: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    exec(args[0], args);
    // exec only returns if it failed
    fprintf(2, "time1: exec %s failed\n", args[0]);
    exit(1);
  }

  wait(0);
  int end = uptime();

  printf("elapsed time: %d ticks\n", end - start);
  exit(0);
}
