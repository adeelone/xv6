#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

// schedtest <work> <prio1> [prio2 ...]
//
// Starts one CPU-bound child per priority given. All children are
// released at the same tick (T0), each does the same amount of work,
// and each reports:
//   response   = first tick it got the CPU after T0
//   turnaround = tick it finished minus T0
// The parent prints a table and the averages. Used for Tasks 4 and 5.

#define MAXKIDS 16

struct result {
  int pid;
  int prio;
  int response;
  int turnaround;
};

// Burn CPU. One unit is roughly one tick on QEMU (calibrated by hand).
void
spin(int units)
{
  volatile int x = 0;
  for (int u = 0; u < units; u++)
    for (int i = 0; i < 30000000; i++)
      x++;
}

int
main(int argc, char *argv[])
{
  int fd[2], n, i, work, t0;
  struct result r, results[MAXKIDS];

  if (argc < 3) {
    printf("usage: schedtest <work> <prio1> [prio2 ...]\n");
    exit(1);
  }
  work = atoi(argv[1]);
  n = argc - 2;
  if (n > MAXKIDS)
    n = MAXKIDS;

  pipe(fd);

  // Run the parent at the top priority so it can create every child
  // before any of them start their real work.
  setpriority(MAXPRIORITY);
  t0 = uptime() + 3;       // common release time for all children

  for (i = 0; i < n; i++) {
    if (fork() == 0) {
      close(fd[0]);
      r.pid = getpid();
      r.prio = atoi(argv[i + 2]);
      setpriority(r.prio);
      // Sleep until T0 so every child becomes RUNNABLE on the same tick.
      if (t0 > uptime())
        pause(t0 - uptime());
      r.response = uptime() - t0;    // first time we got the CPU
      spin(work);
      r.turnaround = uptime() - t0;  // finished
      write(fd[1], &r, sizeof(r));
      exit(0);
    }
  }
  close(fd[1]);

  for (i = 0; i < n; i++)
    wait(0);

  int sumresp = 0, sumturn = 0, got = 0;
  printf("pid\tprio\tresponse\tturnaround   (ticks, in finishing order)\n");
  while (got < n && read(fd[0], &results[got], sizeof(r)) == sizeof(r)) {
    r = results[got];
    printf("%d\t%d\t%d\t\t%d\n", r.pid, r.prio, r.response, r.turnaround);
    sumresp += r.response;
    sumturn += r.turnaround;
    got++;
  }
  if (got > 0) {
    // print averages with one decimal place
    printf("average response   = %d.%d ticks\n",
           sumresp / got, (sumresp * 10 / got) % 10);
    printf("average turnaround = %d.%d ticks\n",
           sumturn / got, (sumturn * 10 / got) % 10);
  }
  close(fd[0]);
  exit(0);
}
