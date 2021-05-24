#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  
  p->ctime = ticks;


  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->priority = 10;
  p->sched_queue = PRIORITY;
  p->arrival_time_ratio = 1;
  p->priority_ratio = 1;
  p->executed_cycle_ratio = 1;
  p->arrival_time = ticks;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->ctime = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
int 
generate_random_priority(int mod)
{
  int random_ticket;
  random_ticket = (13780810*ticks + 13780825*ticks + 13790629*ticks) % mod;
  return random_ticket;
}

struct proc* 
fcfs_scheduler(void)
{
  struct proc* p;
  //int sum_tickets = 1;
  int cr_time = 0;
  int label = 0;
  struct proc* created_sooner = 0;

  for(p = ptable.proc ; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state != RUNNABLE || p->sched_queue != FCFS)
        continue;

    if(label == 0)
    {
      label = 1;
      cr_time = p->ctime;
    }

    if( p->ctime <= cr_time && label == 1)
    {
      cr_time = p->ctime;
      created_sooner = p;
    }

  }
  return created_sooner; 
}

struct proc* 
priority_scheduler(void)
{
  struct proc* p;
  
  int label = 0;
  int random_ticket = 0;
  struct proc* high_priority_order = 0;

  for(p = ptable.proc ; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state != RUNNABLE || p->sched_queue != PRIORITY)
        continue;


    if(label == 0)
    {
      label = 1;
      random_ticket = p->priority;
    }
    
    if( p->priority <= random_ticket && label == 1)
    {
      random_ticket = p->priority;
      high_priority_order = p;
    }

  }
  return high_priority_order; 
  
}

struct proc* 
bjf_scheduler(void)
{
  struct proc* p;
  double min_rank, cur_rank;
  int min_is_set = 0;
  struct proc* min_rank_process = 0;

  for(p = ptable.proc ; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state != RUNNABLE || p->sched_queue != BJF)
        continue;
    if (min_is_set == 1)
    {
      cur_rank = ((1.0/p->priority)*p->priority_ratio)+(p->arrival_time*p->arrival_time_ratio)+(p->executed_cycle*0.1*p->executed_cycle_ratio);
      if (cur_rank < min_rank)
      {
        min_rank = cur_rank;
        min_rank_process = p;
      }
    }
    else
    {
      min_rank_process = p;
      min_rank = ((1.0/p->priority)*p->priority_ratio)+(p->arrival_time*p->arrival_time_ratio)+(p->executed_cycle*0.1*p->executed_cycle_ratio);
      min_is_set = 1;
    }
  }
  if (min_is_set == 0)
    return 0;
  return min_rank_process;
}

struct proc* 
round_robin_scheduler(int* index)
{
  struct proc *p;
  int selected_index = -1;
  int cur_index;
  for (int i = 0; i < NPROC; i++)
  {
    cur_index = (i + (*index)) %  NPROC;
    if(ptable.proc[cur_index].state != RUNNABLE || ptable.proc[cur_index].sched_queue != ROUND_ROBIN)
      continue;

    p = &ptable.proc[cur_index];
    selected_index = cur_index;
    *index = (cur_index + 1) % NPROC;
    break;
  }
  if (selected_index == -1)
    return 0;
  return p;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct proc *ap;
  struct cpu *c = mycpu();
  c->proc = 0;
  int RR_index = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.

    acquire(&ptable.lock);

    p = round_robin_scheduler(&RR_index);

    if (p == 0)
    {
      RR_index = 0;
      p = priority_scheduler();
    }

    if (p == 0)
    {
      p = bjf_scheduler();
    }

    if (p == 0)
    {
      p = fcfs_scheduler();
    }
    if(p != 0)
    {
      p->executed_cycle++;

      for(ap = ptable.proc ; ap < &ptable.proc[NPROC]; ap++)
      {
        if(ap->pid == 0)
          continue;

        if(ap->state == RUNNABLE)
        {
          ap->waiting_time++;
        }
      //  aging
        if (ap->waiting_time > 10000)
        {
          if (ap->sched_queue > 1) 
          {
            ap->sched_queue--;
            ap->waiting_time = 0;
          }
        }
      }

      p->waiting_time = 0;

      c->proc = p;

      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      c->proc = 0;
    }

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void show_descendant(int parent_pid)
{

  int have_descendant = 0;
  int ids[30];
  int d = 0;
  int c,a = 0;
  int crtime[30];
  for(int i = 0; i < NPROC; i++)
  {
    if(ptable.proc[i].parent->pid == parent_pid)
    {
      
      ids[d] = ptable.proc[i].pid;
      crtime[d] = ptable.proc[i].ctime;
      d++;
      
      have_descendant = 1;
    }
  }

  for(int m = 0; m < d; m++)
  {
    for(int n = m + 1; n < d; n++)
    {
      if(crtime[m] < crtime[n])
      {
        c = crtime[m];
        a = ids[m];
        crtime[m] = crtime[n];
        ids[m] = ids[n];
        crtime[n] = c;
        ids[n] = a;
      }
    }
  }
  for(int m = 0; m < d; m++)
    cprintf("my id: %d, my child id: %d, my child creationtime: %d", parent_pid,ids[m],crtime[m]);

  if(have_descendant)
    cprintf("\n");
  for(int m = 0; m < d; m++)
    show_descendant(ids[m]);

}

void show_ancestors(int my_pid)
{

  for(int i = 0; i < NPROC; i++)
  {
    if(ptable.proc[i].pid == my_pid)
    {
      
      cprintf("my id: %d, my parent id: %d, my parent creationtime: %d", my_pid,ptable.proc[i].parent->pid,my_pid,ptable.proc[i].parent->ctime);
      cprintf("\n");
      show_ancestors(ptable.proc[i].parent->pid);
    }
  }

}


void sleep_process(void *chan)
{
  struct proc *p = myproc();
  cprintf("my id: %d", p->pid );
  if(p == 0)
    panic("sleep");


  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  // sched();
  cprintf("my id: %d\n", p->pid );


  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  // if(lk != &ptable.lock){  //DOC: sleeplock2
  //   release(&ptable.lock);
  //   // acquire(lk);
  // }  
}

void change_sched_queue(int pid, int dst_queue)
{
  struct proc* p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid)
    {
      p->sched_queue = dst_queue;
      break;
    }
  }
}




void set_ratio_process(int pid, int priority_ratio, int arrival_time_ratio, int executed_cycle_ratio)
{
  struct proc* p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid)
    {
      p->priority_ratio = priority_ratio;
      p->arrival_time_ratio = arrival_time_ratio;
      p->executed_cycle_ratio = executed_cycle_ratio;
      // cprintf("the new ratios  %d \nthe new ratios  %d \nthe new ratios  %d \n",
      //         p->priority_ratio,p->arrival_time_ratio,p->executed_cycle_ratio);
      break;
    }
  }
}


void set_priority(int pid, int priority)
{
  struct proc* p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid)
    {
      p->priority = priority;
      break;
    }
  }
}




char* get_state(int state){
  if(state == 0){
    return "UNUSED";
  }else if(state == 1){
    return "EMBRYO";
  }else if(state == 2){
    return "SLEEPING";
  }else if(state == 3){
    return "RUNNABLE";
  }else if(state == 4){
    return "RUNNING";
  }else if(state == 5){
    return "ZOMBIE";
  }else{
    return "";
  }
}

char* get_Q_name(int Q){
  if(Q == ROUND_ROBIN){
    return "ROUND_ROBIN";
  }else if(Q == PRIORITY){
    return "PRIORITY";
  }else if(Q == BJF){
    return "BJF";
  }else if(Q == FCFS){
    return "FCFS";    
  }else{
    return "-";
  }
}


void itos(int in, char* res, int* index)
{
  if(in == 0) return;
  int digit = in%10;
  in /= 10;
  itos(in, res, index);
  res[(*index)++] = digit + '0';
}

char* double_to_string(double in, char* res, int afterPoint)
{
  int index = 0;
  int int_part;
  double double_part;
  int_part = (int)in;
  double_part = (double) in - int_part;
  if (int_part == 0) res[index++] = '0';
  itos(int_part, res, &index);
  res[index++] = '.';
  for (int j = 0; j < afterPoint; j++)
  {
    double_part *= 10;
    if (double_part == 0) res[index++] = '0';
  }
  int_part = (int)double_part;
  itos(int_part, res, &index);
  res[index] = '\0';
  return res;
}

int nod(int in)
{
  if (in == 0) return 1;
  int ans = 0;
  while (in > 0)
  {
    in /= 10;
    ans++;
  }
  return ans;
}

void print_processes_details(void)
{
  struct proc *p;
  double rank;
  char buf[16];

  cprintf("name                pid   state       Qnum           priority   ratios           rank          exeCycle    waiting_time\n");
  cprintf("-----------------------------------------------------------------------------------------------------------------------\n");

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if(p->pid == 0)
      continue;
    
    cprintf("%s", p->name);
    for (int i = 0; i < 20 - strlen(p->name); i++) cprintf(" ");
    cprintf("%d", p->pid);
    for (int i = 0; i < 6 - nod(p->pid); i++) cprintf(" ");
    cprintf("%s", get_state(p->state));
    for (int i = 0; i < 12 - strlen(get_state(p->state)); i++) cprintf(" ");
    cprintf("%s", get_Q_name(p->sched_queue));
    for (int i = 0; i < 15 - strlen(get_Q_name(p->sched_queue)); i++) cprintf(" ");
    cprintf("%d", p->priority);
    for (int i = 0; i < 10 - nod(p->priority); i++) cprintf(" ");
    cprintf("%d, %d, %d", p->priority_ratio, p->arrival_time_ratio, p->executed_cycle_ratio);
    for (int i = 0; i < 13-nod(p->priority_ratio)-nod(p->arrival_time_ratio)-nod(p->executed_cycle_ratio); i++) cprintf(" ");
    rank = ((1.0/p->priority)*p->priority_ratio)+(p->arrival_time*p->arrival_time_ratio)+(p->executed_cycle*0.1*p->executed_cycle_ratio);
    cprintf("%s",double_to_string(rank, buf, 3));
    for (int i = 0; i < 14 - strlen(buf); i++) cprintf(" ");
    cprintf("%d", p->executed_cycle);
    for (int i = 0; i < 12 - nod(p->executed_cycle); i++) cprintf(" ");
    cprintf("%d\n", p->waiting_time);
  }
}

