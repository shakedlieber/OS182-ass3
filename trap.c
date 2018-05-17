#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  case T_PGFLT:
    cprintf("page fault pid %d\n",proc->pid);

    proc->numberOfPageFaults++;
    /**  the virtual address stored in %CR2 is stored in page **/
    uint page = PGROUNDDOWN(rcr2());
    int i;
    int count = 0;
    for(i=0;i<MAX_TOTAL_PAGES;i++)
      if( ( proc->pagesDS[i].file_offset != -1 ) && ( proc->pagesDS[i].in_RAM ) )
          count++;
    if(count != MAX_PSYC_PAGES)
      swapToFile(proc->pgdir);

    char* newPageAddress = kalloc();
    if(newPageAddress == 0){
      /**  kalloc failed, didn't try to allocate more memory no need to dealloc **/
      cprintf("kalloc failed in trap PGFLT case\n");
      break;
    }
    memset(newPageAddress, 0, PGSIZE);
    i=0;
    while((i<MAX_TOTAL_PAGES)&&(proc->pagesDS[i].v_address != page))
      i++;
    uint offset = proc->pagesDS[i].file_offset;
    /** populate the page starting at newPageAddress with the info from swap file **/
    readFromSwapFile(proc, newPageAddress, offset, PGSIZE);
    pte_t *pte = walkpgdir_global(proc->pgdir,(void *) page,  0);
    mappages_global(proc->pgdir, (void *) page, PGSIZE, v2p(newPageAddress), PTE_W | PTE_U );

    *pte = PTE_P_ON(*pte);
    *pte = PTE_PG_OFF(*pte);

    /**  we failed at acquiring the page, defenitly going to use it  **/
    *pte = PTE_A_OFF(*pte);
    proc->pagesDS[i].in_RAM = 1;
    proc->pagesDS[i].file_offset = -1;
    /**  REMOVED a page from the swap file, decrement the number of pages in the file ! **/
    proc->pagesInSwpFile--;
    
    lapiceoi();
    break; // PGFLT case break


  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER){

#if (defined(NFUA) || defined(LAPA))
    agePages();
#else if (defined(AQ))
    advanceQueue();
#endif

    yield();
  }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
