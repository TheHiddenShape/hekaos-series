#include "kmalloc.h"
#include "kpanic.h"
#include "paging.h"
#include "printk.h"
#include <stddef.h>
#include <stdint.h>

// align sz up to the nearest 8-byte boundary
#define ALIGN8(sz) (((sz) + 7) & ~(uint32_t)7)

// minimum viable split remainder: header + 8 bytes of usable data
#define BLOCK_MIN (sizeof (kblock_t) + 8)

typedef struct kblock
{
    uint32_t size;
    uint32_t used;
    struct kblock *next;
    struct kblock *prev;
} kblock_t;

static kblock_t *heap_head = NULL;
static uint8_t *heap_end = (uint8_t *)KHEAP_VIRT_BASE;

/* heap_grow – map enough pages to hold at least nbytes of usable heap space,
 * then append (or extend) a free block at the tail. nbytes == 0 is valid and
 * requests a single initial page.*/
static void
heap_grow (uint32_t nbytes)
{
    /* pages >= (nbytes + sizeof(kblock_t) + PAGE_SIZE - 1) / PAGE_SIZE */
    uint32_t pages
        = (nbytes + (uint32_t)sizeof (kblock_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    if (!pages)
    {
        pages = 1;
    }

    uint32_t total = pages * PAGE_SIZE;

    if ((uint32_t)heap_end + total > KHEAP_VIRT_MAX)
    {
        kpanic ("kmalloc: kernel heap limit reached");
    }

    // Map each new page via the paging layer
    for (uint32_t i = 0; i < pages; i++)
    {
        void *vaddr = (void *)(heap_end + i * PAGE_SIZE);
        if (!alloc_page (vaddr, PAGE_PRESENT | PAGE_RW))
        {
            kpanic ("kmalloc: alloc_page failed in heap_grow");
        }
    }

    // Walk to the last block
    kblock_t *last = NULL;
    kblock_t *cur = heap_head;
    while (cur)
    {
        last = cur;
        cur = cur->next;
    }

    /* If the last block is free and sits right at heap_end, just grow it
     * instead of creating a new header (avoids fragmentation at boundaries).*/
    if (last && !last->used
        && ((uint8_t *)last + sizeof (kblock_t) + last->size == heap_end))
    {
        last->size += total;
    }
    else
    {
        kblock_t *blk = (kblock_t *)heap_end;
        blk->size = total - (uint32_t)sizeof (kblock_t);
        blk->used = 0;
        blk->next = NULL;
        blk->prev = last;
        if (last)
        {
            last->next = blk;
        }
        else
        {
            heap_head = blk;
        }
    }

    heap_end += total;
}

void
heap_init (void)
{
    heap_head = NULL;
    heap_end = (uint8_t *)KHEAP_VIRT_BASE;
    heap_grow (0); // map first page and prime the free list
    pr_info ("Heap initialized: base=0x%x end=0x%x\n", KHEAP_VIRT_BASE,
             (uint32_t)heap_end);
    printk ("\n");
}

/* kbrk – extend the heap by at least nbytes. nbytes == 0 returns the current
 * end without mapping anything. Returns the new heap_end. =*/
void *
kbrk (uint32_t nbytes)
{
    if (!nbytes)
    {
        return heap_end;
    }
    heap_grow (nbytes);
    return heap_end;
}

// kmalloc – use for small, sub-page kernel objects (structs, buffers < 1 page).
// For large or page-granularity allocations use vmalloc to avoid heap fragmentation.
/* first-fit allocator. Alignment: all returned pointers are 8-byte aligned
 * (ALIGN8 on size, and KHEAP_VIRT_BASE is page-aligned so headers are
 * naturally 8-aligned). */
void *
kmalloc (size_t size)
{
    if (!size)
    {
        return NULL;
    }

    size = ALIGN8 (size);

    // First-fit search through the block list
    kblock_t *blk = heap_head;
    while (blk)
    {
        if (!blk->used && blk->size >= size)
        {
            break;
        }
        blk = blk->next;
    }

    // No suitable free block – grow the heap and retry
    if (!blk)
    {
        heap_grow (size);
        blk = heap_head;
        while (blk)
        {
            if (!blk->used && blk->size >= size)
            {
                break;
            }
            blk = blk->next;
        }
        if (!blk)
        {
            kpanic ("kmalloc: out of memory");
        }
    }

    // Split if the leftover is large enough to form its own block
    if (blk->size >= size + BLOCK_MIN)
    {
        kblock_t *split
            = (kblock_t *)((uint8_t *)blk + sizeof (kblock_t) + size);
        split->size = blk->size - size - (uint32_t)sizeof (kblock_t);
        split->used = 0;
        split->next = blk->next;
        split->prev = blk;
        if (blk->next)
        {
            blk->next->prev = split;
        }
        blk->next = split;
        blk->size = size;
    }

    blk->used = 1;
    return (uint8_t *)blk + sizeof (kblock_t);
}

/*kfree – return a block to the free list and coalesce with adjacent free
 * neighbours (both directions).*/
void
kfree (void *ptr)
{
    if (!ptr)
    {
        return;
    }

    kblock_t *blk = (kblock_t *)((uint8_t *)ptr - sizeof (kblock_t));

    if (!blk->used)
    {
        kpanic ("kfree: double free detected");
    }

    blk->used = 0;

    // Coalesce forward: merge with next if it is free
    if (blk->next && !blk->next->used)
    {
        kblock_t *nxt = blk->next;
        blk->size += (uint32_t)sizeof (kblock_t) + nxt->size;
        blk->next = nxt->next;
        if (nxt->next)
        {
            nxt->next->prev = blk;
        }
    }

    // Coalesce backward: merge into prev if it is free
    if (blk->prev && !blk->prev->used)
    {
        kblock_t *prv = blk->prev;
        prv->size += (uint32_t)sizeof (kblock_t) + blk->size;
        prv->next = blk->next;
        if (blk->next)
        {
            blk->next->prev = prv;
        }
    }
}

// ksize – return the usable byte count of an allocated block
size_t
ksize (void *ptr)
{
    if (!ptr)
    {
        return 0;
    }
    return ((kblock_t *)((uint8_t *)ptr - sizeof (kblock_t)))->size;
}

void
kmalloc_test (void)
{
    pr_info ("#### kmalloc test ####\n");

    // 1. Basic alloc + write/read
    {
        uint32_t *p = kmalloc (sizeof (uint32_t));
        if (!p)
        {
            kpanic ("kmalloc test: basic alloc returned NULL");
        }
        *p = 0xDEADBEEF;
        if (*p != 0xDEADBEEF)
        {
            kpanic ("kmalloc test: write/read failed");
        }
        pr_info ("kmalloc: alloc+write at 0x%x\n", (uint32_t)p);
        kfree (p);
    }

    // 2. ksize reports at least the requested size
    {
        void *p = kmalloc (100);
        if (!p)
        {
            kpanic ("kmalloc test: ksize alloc returned NULL");
        }
        if (ksize (p) < 100)
        {
            kpanic ("kmalloc test: ksize too small");
        }
        pr_info ("kmalloc: ksize = %u (requested 100)\n", (uint32_t)ksize (p));
        kfree (p);
    }

    // 3. Multiple allocs return unique, non-overlapping pointers
    {
        void *p1 = kmalloc (32);
        void *p2 = kmalloc (64);
        void *p3 = kmalloc (128);
        if (!p1 || !p2 || !p3)
        {
            kpanic ("kmalloc test: multi-alloc returned NULL");
        }
        if (p1 == p2 || p2 == p3 || p1 == p3)
        {
            kpanic ("kmalloc test: duplicate pointers");
        }
        pr_info ("kmalloc: 3 unique allocs p1=0x%x p2=0x%x p3=0x%x\n",
                 (uint32_t)p1, (uint32_t)p2, (uint32_t)p3);

        // 4. kfree + reuse: p4 must fall within the heap range
        kfree (p1);
        void *p4 = kmalloc (16);
        if (!p4)
        {
            kpanic ("kmalloc test: realloc after free returned NULL");
        }
        if ((uint32_t)p4 < KHEAP_VIRT_BASE || (uint32_t)p4 > KHEAP_VIRT_MAX)
        {
            kpanic ("kmalloc test: reallocated pointer outside heap");
        }
        pr_info ("kmalloc: realloc after free at 0x%x\n", (uint32_t)p4);

        // 5. kfree(NULL) is a no-op
        kfree (NULL);
        pr_info ("kmalloc: kfree(NULL) is no-op\n");

        /* 6. Coalesce + large alloc: After freeing everything, a single
         * contiguous block should be able to satisfy a larger request. */
        kfree (p4);
        kfree (p2);
        kfree (p3);
        void *big = kmalloc (512);
        if (!big)
        {
            kpanic ("kmalloc test: large alloc after full coalesce failed");
        }
        pr_info ("kmalloc: 512-byte alloc after coalesce at 0x%x\n",
                 (uint32_t)big);
        kfree (big);
    }

    // 7. kbrk extends the heap
    {
        void *end_before = kbrk (0);
        kbrk (PAGE_SIZE);
        void *end_after = kbrk (0);
        if ((uint32_t)end_after <= (uint32_t)end_before)
        {
            kpanic ("kmalloc test: kbrk did not extend heap");
        }
        pr_info ("kmalloc: kbrk extended heap 0x%x -> 0x%x\n",
                 (uint32_t)end_before, (uint32_t)end_after);
    }

    pr_info ("kmalloc test passed\n\n");
}
