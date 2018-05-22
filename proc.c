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

int initial_size;


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

  p->fileOffset = 0;
  p->numberOfPagedOut = 0;
  p->numberOfPageFaults = 0;
  p->totalNumberOfPagedOut = 0;
  p->numberOfAllocatedPages = 0;

  int i;
  for (i = 0; i < MAX_TOTAL_PAGES; i++)
  {
    p->pagesDS[i].v_address = 0;
    p->pagesDS[i].file_offset = -1;
    p->pagesDS[i].in_RAM = 0;
    p->pagesDS[i].isAllocated = 0;
#if defined(LAPA)
    p->pagesDS[i].age = 0xFFFFFFFF;
#else
    p->pagesDS[i].age = 0x00000000;
#endif
  }

  for(i=0 ; i< MAX_PSYC_PAGES; i++) {
    p->inRAMQueue[i] = -1;
    p->availableOffsetQueue[i] = -1;
  }

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  initial_size = getCurrentCapacity();

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
    curproc->numberOfAllocatedPages +=(PGROUNDUP(n)/PGSIZE);
    int flag = 1;
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n, flag)) == 0)
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

#if (defined(SCFIFO) || defined(NFUA) || defined(AQ) || defined(LAPA))
  if(pid > DEFAULT_PROCESSES)
  {
    /** only new processes need to create page swap (i.e excluding init and shell) **/
    createSwapFile(np);
    np->fileOffset = curproc->fileOffset;
    np->numberOfPagedOut = curproc->numberOfPagedOut;
    np->numberOfAllocatedPages = curproc->numberOfAllocatedPages;
    np->numberOfPageFaults = 0;
    np->totalNumberOfPagedOut = 0;

    int i;
    for (i = 0; i < MAX_TOTAL_PAGES; i++)
    {
      np->pagesDS[i].v_address = curproc->pagesDS[i].v_address;
      np->pagesDS[i].file_offset = curproc->pagesDS[i].file_offset;
      np->pagesDS[i].in_RAM = curproc->pagesDS[i].in_RAM;
      np->pagesDS[i].isAllocated = curproc->pagesDS[i].isAllocated;
      np->pagesDS[i].age = curproc->pagesDS[i].age;
    }
    char* newPage = kalloc();
    for(i=0; i< curproc->numberOfPagedOut;i++)
    {
      readFromSwapFile(curproc,newPage,i*PGSIZE,PGSIZE);
      writeToSwapFile(np,newPage,i*PGSIZE,PGSIZE);
    }

    for(i=0 ; i< MAX_PSYC_PAGES; i++) {
      np->inRAMQueue[i] = curproc->inRAMQueue[i];
      np->availableOffsetQueue[i] = curproc->availableOffsetQueue[i];
    }

    kfree(newPage);
  }
#endif

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

#if (defined(SCFIFO) || defined(NFUA) || defined(AQ) || defined(LAPA))
  removeSwapFile(curproc);
#endif

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
#if (defined(VERBOSE_PRINT_TRUE))
cprintf("%d state=ZOMBIE alloc-memory-pages=%d paged-out=%d page-faults=%d  paged-out-total-num=%d %s\n",
              curproc->pid,curproc->numberOfAllocatedPages,curproc->numberOfPagedOut,
              curproc->numberOfPageFaults,curproc->totalNumberOfPagedOut, curproc->name);
#endif

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
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

#if (defined(NFUA) || defined(LAPA))
      agePages();
#elif (defined(AQ))
      advanceQueue();
#endif

      // Process is done running for now.
      // It should have changed its p->state before coming back.
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
  // int i;
  struct proc *p;
  char *state;
  // uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d state=%s alloc-memory-pages=%d paged-out=%d page-faults=%d  paged-out-total-num=%d %s", p->pid, state,p->numberOfAllocatedPages,p->numberOfPagedOut,
            p->numberOfPageFaults,p->totalNumberOfPagedOut, p->name);
    // cprintf("%d %s %s", p->pid, state, p->name);
    // if(p->state == SLEEPING){
    //   getcallerpcs((uint*)p->context->ebp+2, pc);
    //   for(i=0; i<10 && pc[i] != 0; i++)
    //     cprintf(" %p", pc[i]);
    // }
    cprintf("\n");
  }
  int currentFree = getCurrentCapacity();
  cprintf("%d % free pages in the system\n",((currentFree*100)/initial_size));
}


void fixQueue(int index){
  struct proc *curproc = myproc();
  while(index < MAX_PSYC_PAGES - 1) {
    curproc->inRAMQueue[index] = curproc->inRAMQueue[index + 1];
    index++;
  }
  curproc->inRAMQueue[MAX_PSYC_PAGES-1] = -1;
}

int removeSCFIFO(){
  struct proc *curproc = myproc();
  int index = -1;
  int i;
  int temp[MAX_PSYC_PAGES];

  for (i = 0; i<MAX_PSYC_PAGES; i++)
    temp[i] = -1;

  int temp_index = 0;
  for (i = 0; (i<MAX_PSYC_PAGES)&&(index == -1); i++) {
    uint temp_v_addr = curproc->pagesDS[curproc->inRAMQueue[i]].v_address;
    pde_t* pte = walkpgdir_global(curproc->pgdir, (char*) temp_v_addr, 0);
    if(*pte & PTE_A){
      *pte = PTE_A_OFF(*pte);
      temp[temp_index] = curproc->inRAMQueue[i];
      temp_index++;
    } else{
      index = curproc->inRAMQueue[i];
      break;
    }
  }
  // cprintf("\nscfifo queue index found: %d\n", i);
  if(index == -1){
    index = curproc->inRAMQueue[0];
    if(index != -1){
      fixQueue(0);
    }
    else
      panic("error in removing scfifo!");
    return index;
  }

  i++;
  int new_index = 0;
  while ((i < MAX_PSYC_PAGES) && (curproc->inRAMQueue[i] != -1)) {
    curproc->inRAMQueue[new_index] = curproc->inRAMQueue[i];
    i++;
    new_index++;
  }


  i = 0;
  while (new_index < MAX_PSYC_PAGES) {
    curproc->inRAMQueue[new_index] = temp[i];
    i++;
    new_index++;
  }

  return index;
}

int removeNFUA(){
  struct proc *curproc = myproc();
  int i;
  int min_index = 0;
  for( i =1; i <MAX_PSYC_PAGES; i++) {
    if(curproc->pagesDS[curproc->inRAMQueue[i]].age < curproc->pagesDS[curproc->inRAMQueue[min_index]].age)
      min_index = i;
  }

  i = min_index;
  min_index = curproc->inRAMQueue[min_index];
  fixQueue(i);
  return min_index;
}

int removeLAPA(){
  struct proc *curproc = myproc();
  int i;
  int min_i = -1;
  int min_age = 0xFFFFFFFF;
  int min_count = 33; // sum of all bits = 32
  for(i = 0; i < MAX_PSYC_PAGES; i++) {
    int curr_age = curproc->pagesDS[curproc->inRAMQueue[i]].age;
    int curr_count = 0;
    for (int j = 0; j<32; j++) {
      if ((1 << j) & curr_age)
        curr_count++;
    }
    // cprintf("%x -> %d\n", curr_age, curr_count);
    if (curr_count < min_count) {
      min_i = i;
      min_age = curr_age;
      min_count = curr_count;
    }
    else if (curr_count == min_count && curr_age < min_age) {
      min_i = i;
      min_age = curr_age;
    }
  }

  if (min_i == -1)
    panic("LAPA error");

  // cprintf("idx: %d, age: %x, count: %d\n", min_i, min_age, min_count);

  int index = curproc->inRAMQueue[min_i];
  fixQueue(min_i);
  return index;
}

int removeAQ(){
  struct proc *curproc = myproc();
  int index = curproc->inRAMQueue[0];
  fixQueue(0);
  return index;
}

void insert(int index){
  struct proc *curproc = myproc();
  int i = 0;
  while (curproc->inRAMQueue[i] != -1) {
    if (i == MAX_PSYC_PAGES)
      panic("error in inerstion!");
    i++;
  }
  // cprintf("\n%d <- %d\n", i, index);
  curproc->inRAMQueue[i] = index;
}

void agePages(void){
  struct proc *curproc = myproc();
  int i;

  for(i = 0; i < MAX_TOTAL_PAGES; i++) {
    if(curproc->pagesDS[i].isAllocated == 1) {
      curproc->pagesDS[i].age = curproc->pagesDS[i].age >> 1;
      pte_t* pte = walkpgdir_global(curproc->pgdir, (void*)curproc->pagesDS[i].v_address, 0);
      if(*pte & PTE_A) {
        curproc->pagesDS[i].age = curproc->pagesDS[i].age | 0x80000000;
        *pte = PTE_A_OFF(*pte);
      }
    } 
  }
}

void advanceQueue(void){
  struct proc *curproc = myproc();
  int i;
  for(i = MAX_PSYC_PAGES-1; i < 0 ; i--) {
    if (curproc->inRAMQueue[i] == -1)
      continue;
    int curr_page_idx = curproc->inRAMQueue[i];
    int prev_page_idx = curproc->inRAMQueue[i-1];
    pte_t* pte_curr = walkpgdir_global(curproc->pgdir, (void*)curproc->pagesDS[curr_page_idx].v_address, 0);
    pte_t* pte_pre = walkpgdir_global(curproc->pgdir, (void*)curproc->pagesDS[prev_page_idx].v_address, 0);
    if(((*pte_pre & PTE_A) != 0) && ((*pte_curr & PTE_A) == 0)){
      curproc->inRAMQueue[i] = prev_page_idx;
      curproc->inRAMQueue[i-1] = curr_page_idx;
    }
  }
}

void fixOffsetQueue() {
  struct proc *curproc = myproc();
  int index = 0;
  while (index < MAX_PSYC_PAGES - 1) {
    curproc->availableOffsetQueue[index] = curproc->availableOffsetQueue[index + 1];
    index++;
  }
  curproc->availableOffsetQueue[MAX_PSYC_PAGES -1 ] = -1;
}

int removeOffsetQueue(void) {
  struct proc* curproc = myproc();

  int index = curproc->availableOffsetQueue[0];

  if (index != -1)
    fixOffsetQueue();

  return index;
}

void insertOffsetQueue(int index) {
  struct proc* curproc = myproc();

  int i = 0;
  while (curproc->availableOffsetQueue[i] != -1) {
    if (i == MAX_PSYC_PAGES)
      panic("overflow  in offset insert !");
    i++;
  }
  curproc->availableOffsetQueue[i] = index;

}

int getFreeFileOffset(void) {
  struct proc* curproc = myproc();
  int result = removeOffsetQueue();
  if (result == -1) {
    result = curproc->fileOffset;
    curproc->fileOffset = curproc->fileOffset + PGSIZE;
  }

  return result;
}

void deallocatePage(uint va) {
  struct proc* curproc = myproc();

  int i = 0;
  int idx;
  while (i < MAX_TOTAL_PAGES) {
   // cprintf("WE ARE COMPARING : %p with %p \n", (char*) proc->pagesDS[i].v_address, va);
    if ((curproc->pagesDS[i].isAllocated != 0) && (curproc->pagesDS[i].v_address ==  va)) {

      curproc->pagesDS[i].v_address = 0;
#if defined(LAPA)
      curproc->pagesDS[i].age = 0xFFFFFFFF;
#else
      curproc->pagesDS[i].age = 0x00000000;
#endif
      curproc->pagesDS[i].isAllocated = 0;

      /** If the proc to remove is in the swap file, remember the possible offset **/
      if(curproc->pagesDS[i].in_RAM == 0)
        insertOffsetQueue(curproc->pagesDS[i].file_offset);

      curproc->pagesDS[i].in_RAM = 0;
      curproc->pagesDS[i].file_offset = -1;
      break;

    }
    i++;
  }

  if (i == MAX_TOTAL_PAGES)
    panic("trying to deallocate a non existing page ");

  idx = i;
  i = 0;
  while (i < MAX_PSYC_PAGES) {
    if (curproc->inRAMQueue[i] == idx) {
      fixQueue(i);
      break;
    }
    i++;
  }
}
