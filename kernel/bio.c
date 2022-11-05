// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#define NBUCKETS 13

struct
{
  // struct spinlock lock;
  // mutually exclusive access control for each hash bucket
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;

  // every hashbucket linked list
  struct buf hashbucket[NBUCKETS];
} bcache;

void binit(void)
{
  struct buf *b;

  // initlock(&bcache.lock, "bcache");
  // initialize lock
  for (int i = 0; i < NBUCKETS; i++)
  {
    initlock(&bcache.lock[i], "bcache");
  }

  // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;

  for (int i = 0; i < NBUCKETS; i++)
  {
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    int bucketno = b->blockno % NBUCKETS;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    b->next = bcache.hashbucket[bucketno].next;
    b->prev = &bcache.hashbucket[bucketno];

    initsleeplock(&b->lock, "buffer");

    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    bcache.hashbucket[bucketno].next->prev = b;
    bcache.hashbucket[bucketno].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  // acquire(&bcache.lock);
  int bucketno = blockno % NBUCKETS;
  acquire(&bcache.lock[bucketno]);

  // iterate through the hash bucket, looking for a matching block, if found, return
  for (b = bcache.hashbucket[bucketno].next; b != &bcache.hashbucket[bucketno]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      release(&bcache.lock[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // if there is still an unused data block in the current bucket, reference the data block directly
  for (b = bcache.hashbucket[bucketno].next; b != &bcache.hashbucket[bucketno]; b = b->next)
  {
    if (b->refcnt == 0)
    {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      release(&bcache.lock[bucketno]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // if the corresponding data block is not found in the current bucket and no free data block is available
  for (int i = 0; i < NBUCKETS; i++)
  {
    if (i != bucketno)
    {
      acquire(&bcache.lock[i]);
      for (b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev)
      {
        if (b->refcnt == 0)
        {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;

          b->next->prev = b->prev;
          b->prev->next = b->next;

          b->next = bcache.hashbucket[bucketno].next;
          b->prev = &bcache.hashbucket[bucketno];
          bcache.hashbucket[bucketno].next->prev = b;
          bcache.hashbucket[bucketno].next = b;
          release(&bcache.lock[i]);
          release(&bcache.lock[bucketno]);
          acquiresleep(&b->lock);
          return b;
        }
      }
      release(&bcache.lock[i]);
    }
  }

  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // acquire(&bcache.lock);
  int bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.lock[bucketno]);
  b->refcnt--;
  if (b->refcnt == 0)
  {

    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[bucketno].next;
    b->prev = &bcache.hashbucket[bucketno];
    bcache.hashbucket[bucketno].next->prev = b;
    bcache.hashbucket[bucketno].next = b;
  }

  release(&bcache.lock[bucketno]);
}

void bpin(struct buf *b)
{

  int bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.lock[bucketno]);
  b->refcnt++;
  release(&bcache.lock[bucketno]);
}

void bunpin(struct buf *b)
{

  int bucketno = b->blockno % NBUCKETS;
  acquire(&bcache.lock[bucketno]);
  b->refcnt--;
  release(&bcache.lock[bucketno]);
}
