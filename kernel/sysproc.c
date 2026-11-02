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
    if (addr + n > USYSCALL)
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

// print the calling process's page table on the console.
uint64
sys_vmprint(void)
{
  vmprint(myproc()->pagetable);
  return 0;
}

// pgaccess(base, npages, mask): bit i of *mask is 1 if page
// base + i*PGSIZE was read or written since the last call.
// PTE_A is cleared for the reported pages.
uint64
sys_pgaccess(void)
{
  struct proc *p = myproc();
  uint64 base, umask, mask = 0;
  int npages;
  pte_t *pte;

  argaddr(0, &base);
  argint(1, &npages);
  argaddr(2, &umask);

  if (base % PGSIZE != 0 || npages < 1 || npages > 64)
    return -1;
  // the whole range must be inside the process (written to avoid overflow).
  if (base >= p->sz || (uint64)npages * PGSIZE > p->sz - base)
    return -1;

  // First pass: only read the bits. Pages without a physical page yet
  // (lazy sbrk) are reported as 0 and are not allocated: walk() is
  // called with alloc = 0 and we never go through vmfault().
  for (int i = 0; i < npages; i++) {
    pte = walk(p->pagetable, base + (uint64)i * PGSIZE, 0);
    if (pte && (*pte & PTE_V) && (*pte & PTE_A))
      mask |= 1UL << i;
  }

  if (copyout(p->pagetable, p->sz, umask, (char *)&mask, sizeof(mask)) < 0)
    return -1;

  // Second pass: clear PTE_A only after the result reached the user, so
  // a failed copyout() does not lose accesses.
  for (int i = 0; i < npages; i++) {
    if (mask & (1UL << i)) {
      pte = walk(p->pagetable, base + (uint64)i * PGSIZE, 0);
      *pte &= ~PTE_A;
    }
  }
  // the TLB may hold the entry with A already set: flush it, or the
  // hardware would not set PTE_A again on the next access.
  sfence_vma();
  return 0;
}
