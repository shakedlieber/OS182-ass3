#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

void
initializePagesDataExec(struct page * backupDS, int * backupIndexes, int * queueBackup)
{
  int i;
  for (i = 0; i < MAX_TOTAL_PAGES; ++i) {
    /**  backup proc pagesDS before clean**/
    backupDS[i].file_offset = proc->pagesDS[i].file_offset;
    backupDS[i].in_RAM = proc->pagesDS[i].in_RAM;
    backupDS[i].v_address = proc->pagesDS[i].v_address;
    backupDS[i].isAllocated = proc->pagesDS[i].isAllocated;

    /**  clean proc pagesDS before exec **/
    proc->pagesDS[i].v_address = 0;
    proc->pagesDS[i].file_offset = 0;
    proc->pagesDS[i].in_RAM = 0;
    proc->pagesDS[i].isAllocated = 0;
  }

  for (i = 0; i < MAX_PSYC_PAGES; ++i) {
    /**  backup pages queue **/
    queueBackup[i] = proc->inRAMQueue[i];
    /**  clear pages queue **/
    proc->inRAMQueue[i] = -1;
  }

  /**  backup proc page counters before clean **/
  backupIndexes[0] = proc->numberOfPageFaults;
  backupIndexes[1] = proc->fileOffset;
  backupIndexes[2] = proc->pagesInSwpFile;

  /**  clean proc page counters before exec **/
  proc->numberOfPageFaults = 0;
  proc->fileOffset = 0;
  proc->pagesInSwpFile = 0;

}

void
restoreFromBackup(struct page * backupDS, int * backupIndexes, int *queueBackup)
{
  int i;
  for (i = 0; i < MAX_TOTAL_PAGES; ++i) {
    /**  restore proc pagesDS after failed exec **/
    proc->pagesDS[i].v_address = backupDS[i].v_address;
    proc->pagesDS[i].file_offset = backupDS[i].file_offset;
    proc->pagesDS[i].in_RAM = backupDS[i].in_RAM;
    proc->pagesDS[i].isAllocated = backupDS[i].isAllocated;
  }

  for (i = 0 ; i < MAX_PSYC_PAGES; ++i) {
    proc->inRAMQueue[i] = queueBackup[i];
  }


  /**  restore proc page counters after failed exec **/
  proc->numberOfPageFaults = backupIndexes[0];
  proc->fileOffset = backupIndexes[1];
  proc->pagesInSwpFile = backupIndexes[2];

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

  struct page backupPagesDS[MAX_TOTAL_PAGES];
  int backupIndexes[3];
  initializePagesDataExec(backupPagesDS, backupIndexes);

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

  /**  change to the new swap file**/
  if(proc-> pid > DEFAULT_PROCESSES)
  {
    removeSwapFile(proc);
    createSwapFile(proc);
  }

  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:

  /**  failed exec restore the pagesDS and all indexes and counters stored in backup **/
  restoreFromBackup(backupPagesDS , backupIndexes);

  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
