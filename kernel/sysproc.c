#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_wait2(void)
{
  uint64 status;
  uint64 rusage;
  argaddr(0, &status);
  argaddr(1, &rusage);
  return kwait2(status, rusage);
}

// PA2 Task 1: copy a struct pstat for every process into the
// user array passed as argument 0. Returns the number of processes.
uint64
sys_getprocs(void)
{
  uint64 addr;
  argaddr(0, &addr);
  return kgetprocs(addr);
}

// PA2 Task 2: return the calling process's base priority.
uint64
sys_getpriority(void)
{
  struct proc *p = myproc();
  int prio;

  acquire(&p->lock);
  prio = p->priority;
  release(&p->lock);
  return prio;
}

// PA2 Task 2: set the calling process's base priority.
// Valid priorities are 0..MAXPRIORITY. Returns the old
// priority, or -1 if the new one is out of range.
uint64
sys_setpriority(void)
{
  struct proc *p = myproc();
  int prio, old;

  argint(0, &prio);
  if (prio < 0 || prio > MAXPRIORITY)
    return -1;

  acquire(&p->lock);
  old = p->priority;
  p->priority = prio;
  release(&p->lock);
  return old;
}
