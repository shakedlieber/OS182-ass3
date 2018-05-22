#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

void
initializePagesDataExec(struct page * backupDS, int * backupIndexes, int * queueBackup, int * offsetQueueBackup)
{
  struct proc *curproc = myproc();
  int i;
  for (i = 0; i < MAX_TOTAL_PAGES; ++i) {
    /**  backup proc pagesDS before clean**/
    backupDS[i].file_offset = curproc->pagesDS[i].file_offset;
    backupDS[i].in_RAM = curproc->pagesDS[i].in_RAM;
    backupDS[i].v_address = curproc->pagesDS[i].v_address;
    backupDS[i].isAllocated = curproc->pagesDS[i].isAllocated;

    /**  clean proc pagesDS before exec **/
    curproc->pagesDS[i].v_address = 0;
    curproc->pagesDS[i].file_offset = -1;
    curproc->pagesDS[i].in_RAM = 0;
    curproc->pagesDS[i].isAllocated = 0;
  }

  for (i = 0; i < MAX_PSYC_PAGES; ++i) {
    /**  backup pages queue **/
    queueBackup[i] = curproc->inRAMQueue[i];
    offsetQueueBackup[i] = curproc->availableOffsetQueue[i];
    /**  clear pages queue **/
    curproc->inRAMQueue[i] = -1;
    curproc->availableOffsetQueue[i] = -1;
  }

  /**  backup proc page counters before clean **/
  backupIndexes[0] = curproc->fileOffset;
  backupIndexes[1] = curproc->numberOfPagedOut;
  backupIndexes[2] = curproc->numberOfPageFaults;
  backupIndexes[3] = curproc->totalNumberOfPagedOut;
  backupIndexes[4] = curproc->numberOfAllocatedPages;


  /**  clean proc page counters before exec **/
  curproc->fileOffset = 0;
  curproc->numberOfPagedOut = 0;
  curproc->numberOfPageFaults = 0;
  curproc->totalNumberOfPagedOut = 0;
  curproc->numberOfAllocatedPages = 0;

}

void
restoreFromBackup(struct page * backupDS, int * backupIndexes, int *queueBackup, int * offsetQueueBackup)
{
  struct proc *curproc = myproc();
  int i;
  for (i = 0; i < MAX_TOTAL_PAGES; ++i) {
    /**  restore proc pagesDS after failed exec **/
    curproc->pagesDS[i].v_address = backupDS[i].v_address;
    curproc->pagesDS[i].file_offset = backupDS[i].file_offset;
    curproc->pagesDS[i].in_RAM = backupDS[i].in_RAM;
    curproc->pagesDS[i].isAllocated = backupDS[i].isAllocated;
  }

  for (i = 0 ; i < MAX_PSYC_PAGES; ++i) {
    curproc->inRAMQueue[i] = queueBackup[i];
    curproc->availableOffsetQueue[i] = offsetQueueBackup[i];

  }


  /**  restore proc page counters after failed exec **/
  curproc->fileOffset = backupIndexes[0] ;
  curproc->numberOfPagedOut = backupIndexes[1];
  curproc->numberOfPageFaults = backupIndexes[2];
  curproc->totalNumberOfPagedOut = backupIndexes[3];
  curproc->numberOfAllocatedPages = backupIndexes[4];

}

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

#if (defined(SCFIFO) || defined(NFUA) || defined(AQ) || defined(LAPA))

  struct page backupPagesDS[MAX_TOTAL_PAGES];
  int backupIndexes[5];
  int queueBackup[MAX_PSYC_PAGES];
  int offsetQueueBackup[MAX_PSYC_PAGES];
  initializePagesDataExec(backupPagesDS, backupIndexes, queueBackup, offsetQueueBackup);
#endif

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;


#if (defined(SCFIFO) || defined(NFUA) || defined(AQ) || defined(LAPA))
  /**  change to the new swap file**/
  if(curproc-> pid > DEFAULT_PROCESSES)
  {
    removeSwapFile(curproc);
    createSwapFile(curproc);
  }
#endif

  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:


#if (defined(SCFIFO) || defined(NFUA) || defined(AQ) || defined(LAPA))
  /**  failed exec restore the pagesDS and all indexes and counters stored in backup **/
  restoreFromBackup(backupPagesDS , backupIndexes, queueBackup, offsetQueueBackup);
#endif
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
