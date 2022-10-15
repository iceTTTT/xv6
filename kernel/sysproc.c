#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

struct spinlock vmlock;

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
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

uint64
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

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
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
sys_mmap(void) {
  int sz, prot, maptype, fd;
  if (argint(1, &sz) < 0 || argint(2, &prot) < 0 || argint(3, &maptype) < 0 || argint(4, &fd) < 0) {
    return -1;
  }
  // Store the VMA in process.
  int md;
  struct vma *vma_;
  struct file *tf;
  tf = myproc()->ofile[fd];
  // Check the protlevel && maptype against the file protection level.
  if (maptype == MAP_SHARED && !tf->writable && (prot & PROT_WRITE)) {
    return -1;
  }
  // acquire(&vmlock);
  // acquire(&myproc()->lock);
  
  if ((vma_ = Allocvma()) == (struct vma*)-1) {
    // release(&myproc()->lock);
    // release(&vmlock);
    return -1;
  }
  if ((md = Mralloc(vma_)) == -1) {
    Deallocvma(vma_);
    // release(&myproc()->lock);
    // release(&vmlock);
    return -1;
  }
  vma_->maptype_ = maptype;
  vma_->protlevel_ = prot;
  vma_->size_ = sz;
  vma_->of_ = tf;
  // increase the file reference.
  tf->ref++;
  // Find starting va for this vma. ROUND-UP . Maybe leave some invalid space. maybe wrong.
  vma_->addr_ = PGROUNDUP(myproc()->sz);
  // Change the proc size.
  myproc()->sz = vma_->addr_ + sz;
  vma_->remainpage_ = sz / 4096 ;
  if (sz % 4096) vma_->remainpage_++;
  // release(&myproc()->lock);
  // release(&vmlock);
  return (uint64)vma_->addr_;
}


// Redundent unmap will cause error.
uint64
sys_munmap(void) {
  uint64 addr;
  int size;
  if (argaddr(0, &addr) || argint(1, &size)) {
      return -1;
  }
  struct vma *v;
  int unmapped = -1;
  for (int i = 0; i < NOMAP; i++) {
    if (myproc()->mr_[i] != 0 ) {
      v = myproc()->mr_[i];
      if (addr >= v->addr_ && addr < v->addr_ + v->size_) {
        // Unmap the relavent pages
        while (size > 0) {
          size -= 4096;
          uint64 preaddr = addr;
          pte_t *pte;
          int mapped = 1;
          if((pte = walk(myproc()->pagetable, addr, 0)) == 0)
            panic("uvmunmap: walk");
          if((*pte & PTE_V) == 0)
            mapped = 0;
          if (v->maptype_ == MAP_SHARED && mapped) {
            int n = 4096;
            int r = 0;
            int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
            int d = 0;
            while(d < n){
              int n1 = n - d;
              if(n1 > max)
                n1 = max;

              begin_op();
              ilock(v->of_->ip);
              if ((r = writei(v->of_->ip, 1, addr,  addr - v->addr_, n1)) > 0)
               addr += r;
              iunlock(v->of_->ip);
              end_op();

              if(r != n1){
                // error from writei

                return -1;
              }
              d += r;
            }
          } else { addr += 4096; }
          // Redundent unmap will panic.
          if (mapped)
              uvmunmap(myproc()->pagetable, preaddr, 1, 1);
          if (--v->remainpage_ == 0) {
            v->of_->ref--;
            Deallocvma(v);
            myproc()->mr_[i] = 0;
            break;
          }
        }
        unmapped = 0;
        break;
      }
    }
  }  
  return unmapped;
}


