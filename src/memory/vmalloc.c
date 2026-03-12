#include "vmalloc.h"
#include "kpanic.h"
#include "paging.h"
#include "printk.h"
#include <stdint.h>

#define VMALLOC_PAGES ((VMALLOC_MAX - VMALLOC_BASE + 1) / PAGE_SIZE)
#define BITMAP_WORDS ((VMALLOC_PAGES + 31) / 32)

// one bit per virtual page in the vmalloc zone; 0 = free, 1 = used
static uint32_t bitmap[BITMAP_WORDS];

static inline void
bitmap_set (uint32_t idx)
{
    bitmap[idx / 32] |= (1u << (idx % 32));
}

static inline void
bitmap_clear (uint32_t idx)
{
    bitmap[idx / 32] &= ~(1u << (idx % 32));
}

static inline int
bitmap_test (uint32_t idx)
{
    return (bitmap[idx / 32] >> (idx % 32)) & 1;
}

void
vmalloc_init (void)
{
    for (uint32_t i = 0; i < BITMAP_WORDS; i++)
    {
        bitmap[i] = 0;
    }
    pr_info ("vmalloc zone: 0x%x - 0x%x (%u pages)\n", VMALLOC_BASE,
             VMALLOC_MAX, (uint32_t)VMALLOC_PAGES);
    printk ("\n");
}

/* vmalloc – use for large or page-granularity allocations that would fragment
 * the kmalloc heap. Maps npages contiguous virtual pages on demand;
 * granularity is PAGE_SIZE. Prefer kmalloc for small (<< 1 page) objects. */
void *
vmalloc (uint32_t npages)
{
    if (!npages)
    {
        return NULL;
    }

    // first-fit: find a contiguous run of npages free pages
    uint32_t run = 0;
    uint32_t start = 0;
    for (uint32_t i = 0; i < VMALLOC_PAGES; i++)
    {
        if (!bitmap_test (i))
        {
            if (!run)
            {
                start = i;
            }
            if (++run == npages)
            {
                for (uint32_t j = start; j < start + npages; j++)
                {
                    void *vaddr = (void *)(VMALLOC_BASE + j * PAGE_SIZE);
                    if (!alloc_page (vaddr, PAGE_PRESENT | PAGE_RW))
                    {
                        kpanic ("vmalloc: alloc_page failed");
                    }
                    bitmap_set (j);
                }
                return (void *)(VMALLOC_BASE + start * PAGE_SIZE);
            }
        }
        else
        {
            run = 0;
        }
    }
    return NULL;
}

// vfree – unmap and release npages pages starting at ptr
void
vfree (void *ptr, uint32_t npages)
{
    if (!ptr || !npages)
    {
        return;
    }

    uint32_t vaddr = (uint32_t)ptr;
    if (vaddr < VMALLOC_BASE || vaddr >= VMALLOC_MAX)
    {
        kpanic ("vfree: pointer outside vmalloc zone");
    }

    uint32_t idx = (vaddr - VMALLOC_BASE) / PAGE_SIZE;
    for (uint32_t i = 0; i < npages; i++)
    {
        if (!bitmap_test (idx + i))
        {
            kpanic ("vfree: page not allocated");
        }
        free_page ((void *)(VMALLOC_BASE + (idx + i) * PAGE_SIZE));
        bitmap_clear (idx + i);
    }
}

// vbrk – returns the address of the first free page in the vmalloc zone
void *
vbrk (void)
{
    for (uint32_t i = 0; i < VMALLOC_PAGES; i++)
    {
        if (!bitmap_test (i))
        {
            return (void *)(VMALLOC_BASE + i * PAGE_SIZE);
        }
    }
    return (void *)VMALLOC_MAX;
}

// vsize – vmalloc granularity is always one page
uint32_t
vsize (void)
{
    return PAGE_SIZE;
}

void
vmalloc_test (void)
{
    pr_info ("#### vmalloc test ####\n");

    // 1. Single-page alloc + write/read
    {
        uint32_t *p = vmalloc (1);
        if (!p)
        {
            kpanic ("vmalloc test: single-page alloc returned NULL");
        }
        *p = 0xCAFEBABE;
        if (*p != 0xCAFEBABE)
        {
            kpanic ("vmalloc test: write/read failed");
        }
        pr_info ("vmalloc: 1-page alloc at 0x%x\n", (uint32_t)p);
        vfree (p, 1);
    }

    // 2. Multi-page alloc + touch first and last bytes
    {
        uint8_t *p = vmalloc (4);
        if (!p)
        {
            kpanic ("vmalloc test: 4-page alloc returned NULL");
        }
        p[0] = 0xAA;
        p[4 * PAGE_SIZE - 1] = 0xBB;
        if (p[0] != 0xAA || p[4 * PAGE_SIZE - 1] != 0xBB)
        {
            kpanic ("vmalloc test: multi-page r/w failed");
        }
        pr_info ("vmalloc: 4-page alloc at 0x%x\n", (uint32_t)p);
        vfree (p, 4);
    }

    // 3. Two allocations return distinct non-overlapping addresses
    {
        void *p1 = vmalloc (1);
        void *p2 = vmalloc (1);
        if (!p1 || !p2)
        {
            kpanic ("vmalloc test: unique alloc returned NULL");
        }
        if (p1 == p2)
        {
            kpanic ("vmalloc test: duplicate addresses");
        }
        pr_info ("vmalloc: 2 unique allocs p1=0x%x p2=0x%x\n", (uint32_t)p1,
                 (uint32_t)p2);
        vfree (p1, 1);
        vfree (p2, 1);
    }

    // 4. vbrk falls inside the vmalloc zone
    {
        void *brk = vbrk ();
        if ((uint32_t)brk < VMALLOC_BASE || (uint32_t)brk > VMALLOC_MAX)
        {
            kpanic ("vmalloc test: vbrk outside vmalloc zone");
        }
        pr_info ("vmalloc: vbrk=0x%x\n", (uint32_t)brk);
    }

    // 5. vfree(NULL) is a no-op
    vfree (NULL, 0);
    pr_info ("vmalloc: vfree(NULL) is no-op\n");

    // 6. Reuse after vfree: freed page is recycled
    {
        void *p1 = vmalloc (1);
        if (!p1)
        {
            kpanic ("vmalloc test: reuse alloc returned NULL");
        }
        uint32_t addr1 = (uint32_t)p1;
        vfree (p1, 1);
        void *p2 = vmalloc (1);
        if (!p2)
        {
            kpanic ("vmalloc test: reuse re-alloc returned NULL");
        }
        if ((uint32_t)p2 != addr1)
        {
            kpanic ("vmalloc test: freed page not reused");
        }
        pr_info ("vmalloc: reuse ok p2=0x%x == old p1\n", (uint32_t)p2);
        vfree (p2, 1);
    }

    // 7. Bitmap fragmentation: free middle slot, reuse it, large alloc goes after
    {
        void *a = vmalloc (1);
        void *b = vmalloc (1);
        void *c = vmalloc (1);
        if (!a || !b || !c)
        {
            kpanic ("vmalloc test: frag alloc returned NULL");
        }
        vfree (b, 1); // create a 1-page hole between a and c
        void *d = vmalloc (1);
        if (!d)
        {
            kpanic ("vmalloc test: frag reuse returned NULL");
        }
        if (d != b)
        {
            kpanic ("vmalloc test: first-fit did not reuse hole");
        }
        // 2-page alloc must skip the filled hole and go after c
        void *e = vmalloc (2);
        if (!e)
        {
            kpanic ("vmalloc test: frag 2-page alloc returned NULL");
        }
        if ((uint32_t)e <= (uint32_t)c)
        {
            kpanic ("vmalloc test: 2-page alloc overlaps with c");
        }
        pr_info ("vmalloc: frag ok d=0x%x(==b) e=0x%x(>c=0x%x)\n",
                 (uint32_t)d, (uint32_t)e, (uint32_t)c);
        vfree (a, 1);
        vfree (d, 1);
        vfree (c, 1);
        vfree (e, 2);
    }

    pr_info ("vmalloc test passed\n\n");
}
