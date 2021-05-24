#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_get_descendant(void)
{
  int parent_id;

  if(argint(0, &parent_id) < 0)
    return -1;
  show_descendant(parent_id);
  return 1;
}

int sys_get_ancestors(void)
{
  int my_id;

  if(argint(0, &my_id) < 0)
    return -1;
  show_ancestors(my_id);
  return 1;
}

int sys_get_creation_time(void)
{
  return myproc()->ctime;
}

int sys_calc_perfect_square(void)
{
  struct proc *curproc = myproc();
  int number = curproc->tf->edx;

  int square = 1;
  for(int i = 1; i < number; i++)
  {
    if(i*i <= number)
    {
      square = i * i;
    }
    else
    {
    return square;       
    }
  }
  return square;
}

int sys_sleep_time(void)
{
  
  struct proc *curproc = myproc();
  int n = curproc->tf->edx;
  // int n;
  int ticks0 = 0;
  cprintf("my time bafor sleep: %d\n", ticks );
  cprintf("my id bafor sleep: %d\n", curproc->pid );
  if(argint(0, &n) < 0)
    return -1;
  // acquire(&tickslock);

  while(ticks0 < n){
    if(curproc->killed){
      // release(&tickslock);
      return -1;
    }
    sleep_process(&ticks);
    ticks0 ++;
  }
  cprintf("my id after sleep: %d\n", myproc()->pid );
  cprintf("my time after sleep: %d\n", ticks );
  // release(&tickslock);
  return 0;
}


int sys_change_queue(void)
{
  int pid;
  int dst_queue;

  if(argint(0, &pid) < 0)
    return -1;

  if(argint(1, &dst_queue) < 0)
    return -1;
  
  change_sched_queue(pid, dst_queue);
  return 1;
}


int sys_set_ratio_process(void)
{
  int pid;
  int priority_ratio;
  int arrival_time_ratio;
  int executed_cycle_ratio;

  if(argint(0, &pid) < 0)
    return -1;

  if(argint(1, &priority_ratio) < 0)
    return -1;

  if(argint(2, &arrival_time_ratio) < 0)
    return -1;

  if(argint(3, &executed_cycle_ratio) < 0)
    return -1;
  
  set_ratio_process(pid, priority_ratio, arrival_time_ratio, executed_cycle_ratio);
  return 1;
}

int sys_set_priority(void)
{
  int pid;
  int priority;

  if(argint(0, &pid) < 0)
    return -1;

  if(argint(1, &priority) < 0)
    return -1;
  
  set_priority(pid, priority);
  return 1;
}

int sys_print_processes_details(void)
{
  print_processes_details();
  return 1;
}