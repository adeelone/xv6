#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/pstat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: time command [args...]\n");
    exit(1);
  }

  // Build a null-terminated argument list for exec,
  // skipping argv[0] which is "time" itself.
  char *args[MAXARG];
  int n = 0;
  for(int i = 1; i < argc && n < MAXARG - 1; i++)
    args[n++] = argv[i];
  args[n] = 0;

  int start = uptime();

  int pid = fork();
  if(pid < 0){
    fprintf(2, "time: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    exec(args[0], args);
    // exec only returns if it failed
    fprintf(2, "time: exec %s failed\n", args[0]);
    exit(1);
  }

  // Wait for the child and get its CPU usage from the kernel.
  int status;
  struct rusage ru;
  if(wait2(&status, &ru) < 0){
    fprintf(2, "time: wait2 failed\n");
    exit(1);
  }
  int end = uptime();

  int elapsed = end - start;
  int cpu = ru.cputime;

  // Percent of elapsed time the child spent on the CPU.
  // Guard against dividing by zero, and cap at 100 since
  // tick rounding can make cpu slightly larger than elapsed.
  int pct = 0;
  if(elapsed > 0)
    pct = (cpu * 100) / elapsed;
  if(pct > 100)
    pct = 100;

  printf("elapsed time: %d ticks, cpu time: %d ticks, %d%% CPU\n",
         elapsed, cpu, pct);
  exit(0);
}
