#define NPROC       64                // maximum number of processes
#define NCPU        8                 // maximum number of CPUs
#define NOFILE      16                // open files per process
#define NFILE       100               // open files per system
#define NINODE      50                // maximum number of active i-nodes
#define NDEV        10                // maximum major device number
#define ROOTDEV     1                 // device number of file system root disk
#define MAXARG      32                // max exec arguments
#define MAXOPBLOCKS 10                // max # of blocks any FS op writes
#define LOGBLOCKS   (MAXOPBLOCKS * 3) // max data blocks in on-disk log
#define NBUF        (MAXOPBLOCKS * 3) // size of disk block cache
#define FSSIZE      2000              // size of file system in blocks
#define MAXPATH     128               // maximum file path name
#define USERSTACK   1                 // user stack pages

// ---- PA2: priorities ----
#define MAXPRIORITY    49      // highest base priority a process can set

// ---- PA2: scheduler selection (compile-time) ----
#define SCHED_RR       0   // original xv6 round-robin
#define SCHED_PRIORITY 1   // highest priority runs first

// Pick the policy here, or override from the command line:
//   make qemu SCHED=SCHED_RR
#ifndef SCHEDPOLICY
#define SCHEDPOLICY SCHED_PRIORITY
#endif

// ---- PA2: aging ----
#ifndef AGING
#define AGING 1                // 1 = add aging to the priority scheduler
#endif
#define MAXEFFPRIORITY 99      // highest effective priority after aging
#define AGINGTICKS     1       // +1 effective priority per AGINGTICKS ticks waited
