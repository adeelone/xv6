#ifndef PSTAT_H
#define PSTAT_H

// Process states. Moved here from proc.h so that user programs
// (like ps) can use the same enum without including proc.h.
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Resource usage returned by wait2() (PA1).
struct rusage {
  uint cputime;
};

// One entry per process, filled in by the getprocs() system call.
struct pstat {
  int pid;              // Process ID
  enum procstate state; // Process state
  uint64 size;          // Size of process memory (bytes)
  int ppid;             // Parent process ID
  char name[16];        // Process command name
  int priority;         // Base priority (0..MAXPRIORITY)
  int effpriority;      // Effective priority after aging
  uint readytime;       // Tick when the process became RUNNABLE
  uint cputime;         // Ticks of CPU used so far (from PA1)
};

#endif
