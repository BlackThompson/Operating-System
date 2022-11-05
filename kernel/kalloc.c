// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];

// Black add
// struct kmem kmems[NCPU];

void kinit()
{
  // initlock(&kmem.lock, "kmem");
  for (int i = 0; i < NCPU; i++)
  {
    initlock(&kmems[i].lock, "kmem");
  }
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;
  int current_id;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  // Black add
  // get current CPU id
  push_off();
  current_id = cpuid();
  pop_off();

  acquire(&kmems[current_id].lock);
  r->next = kmems[current_id].freelist;
  kmems[current_id].freelist = r;
  release(&kmems[current_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int current_id;

  // Black add
  // get current CPU id
  push_off();
  current_id = cpuid();
  pop_off();

  acquire(&kmems[current_id].lock);
  r = kmems[current_id].freelist;
  if (r)
    kmems[current_id].freelist = r->next;
  else
  {
    // Black
    // 寻找其他CPU的freelist中的空闲内存块
    for (int i = 0; i < NCPU; i++)
    {
      if (i != current_id)
      {
        // get CPU_id lock
        acquire(&kmems[i].lock);
        r = kmems[i].freelist;
        if (r)
        {
          // 在对应CPU的freelist取下一页
          kmems[i].freelist = r->next;
          release(&kmems[i].lock);
          break;
        }
        release(&kmems[i].lock);
      }
    }
  }
  release(&kmems[current_id].lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
