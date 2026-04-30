#include "kmalloc.h"
#include "kpanic.h"
#include "paging.h"
#include "printk.h"
#include <stdint.h>

typedef struct cpool_hdr
{
    uint32_t obj_size;
    uint16_t free_count;
    uint16_t total_count;
} cpool_hdr_t; /* 8 bytes */

#define CPOOL_HDR_SIZE ((uint32_t)sizeof (cpool_hdr_t))
#define CPOOL_DATA_SIZE (PAGE_SIZE - CPOOL_HDR_SIZE)

static const uint32_t cache_sizes[KMEM_CACHE_COUNT] = {
    KMEM_CACHE_8,  KMEM_CACHE_16,  KMEM_CACHE_32,
    KMEM_CACHE_64, KMEM_CACHE_128, KMEM_CACHE_256,
};

struct kmem_cache
{
    uint32_t obj_size;
    void *free_list;
};

static struct kmem_cache caches[KMEM_CACHE_COUNT];
static uint8_t *cpool_vptr = (uint8_t *)KCPOOL_VIRT_BASE;
static uint32_t cpool_page_count = 0;

/* returns index of first cache whose obj_size >= size */
static int
cache_index (uint32_t size)
{
    for (int i = 0; i < KMEM_CACHE_COUNT; i++)
    {
        if (caches[i].obj_size >= size)
        {
            return i;
        }
    }
    return -1;
}

/* returns index of cache whose obj_size == obj_size exactly */
static int
cache_index_exact (uint32_t obj_size)
{
    for (int i = 0; i < KMEM_CACHE_COUNT; i++)
    {
        if (caches[i].obj_size == obj_size)
        {
            return i;
        }
    }
    kpanic ("kfree: invalid obj_size in chunk pool header");
    return -1;
}

static void
cpool_refill (int idx)
{
    if ((uint32_t)cpool_vptr + PAGE_SIZE > KCPOOL_VIRT_MAX)
    {
        kpanic ("kmalloc: chunk pool zone exhausted");
    }

    if (!alloc_page (cpool_vptr, PAGE_PRESENT | PAGE_RW))
    {
        kpanic ("kmalloc: alloc_page failed in cpool_refill");
    }

    uint32_t obj_size = caches[idx].obj_size;
    uint32_t n = CPOOL_DATA_SIZE / obj_size;
    cpool_hdr_t *hdr = (cpool_hdr_t *)cpool_vptr;

    hdr->obj_size = obj_size;
    hdr->free_count = (uint16_t)n;
    hdr->total_count = (uint16_t)n;

    uint8_t *obj = cpool_vptr + CPOOL_HDR_SIZE;
    for (uint32_t i = 0; i < n; i++)
    {
        void *next = (i + 1 < n) ? (void *)(obj + obj_size) : NULL;
        *(void **)obj = next;
        obj += obj_size;
    }

    caches[idx].free_list = cpool_vptr + CPOOL_HDR_SIZE;
    cpool_vptr += PAGE_SIZE;
    cpool_page_count++;
}

void
kmalloc_init (void)
{
    cpool_vptr = (uint8_t *)KCPOOL_VIRT_BASE;
    cpool_page_count = 0;

    for (int i = 0; i < KMEM_CACHE_COUNT; i++)
    {
        caches[i].obj_size = cache_sizes[i];
        caches[i].free_list = NULL;
    }

    pr_info ("kmalloc (chunk pool) initialized: base=0x%x\n", KCPOOL_VIRT_BASE);
}

void *
kmalloc (size_t size)
{
    if (!size)
    {
        return NULL;
    }

    if (size > KMEM_CACHE_256)
    {
        kpanic ("kmalloc: size > 256, use kmem_alloc for large allocations");
    }

    int idx = cache_index ((uint32_t)size);

    if (!caches[idx].free_list)
    {
        cpool_refill (idx);
    }

    void **chunk = (void **)caches[idx].free_list;
    cpool_hdr_t *hdr = (cpool_hdr_t *)((uintptr_t)chunk & ~(uintptr_t)0xFFF);
    caches[idx].free_list = *chunk;
    hdr->free_count--;

    return (void *)chunk;
}

void
kfree (void *addr)
{
    if (!addr)
    {
        return;
    }

    cpool_hdr_t *hdr = (cpool_hdr_t *)((uintptr_t)addr & ~(uintptr_t)0xFFF);
    int idx = cache_index_exact (hdr->obj_size);

    *(void **)addr = caches[idx].free_list;
    caches[idx].free_list = addr;
    hdr->free_count++;

    if (hdr->free_count == hdr->total_count)
    {
        uintptr_t page_base = (uintptr_t)hdr;
        void **cur = (void **)&caches[idx].free_list;

        while (*cur != NULL)
        {
            if (((uintptr_t)*cur & ~(uintptr_t)0xFFF) == page_base)
            {
                *cur = *(void **)*cur;
            }
            else
            {
                cur = (void **)*cur;
            }
        }

        free_page ((void *)page_base);
        cpool_page_count--;
    }
}

size_t
ksize (void *addr)
{
    if (!addr)
    {
        return 0;
    }

    cpool_hdr_t *hdr = (cpool_hdr_t *)((uintptr_t)addr & ~(uintptr_t)0xFFF);
    return (size_t)hdr->obj_size;
}

void
kmalloc_query (kmalloc_stats_t *s)
{
    s->pool_pages = cpool_page_count;
    s->free_objects = 0;

    for (int i = 0; i < KMEM_CACHE_COUNT; i++)
    {
        void *p = caches[i].free_list;
        while (p)
        {
            s->free_objects++;
            p = *(void **)p;
        }
    }
}

void
kmalloc_test (void)
{
    pr_info ("#### kmalloc (chunk pool) test ####\n");

    /* 1. alloc + write/read + free for each of the 6 cache sizes */
    {
        static const uint32_t sizes[KMEM_CACHE_COUNT] = {
            KMEM_CACHE_8,  KMEM_CACHE_16,  KMEM_CACHE_32,
            KMEM_CACHE_64, KMEM_CACHE_128, KMEM_CACHE_256,
        };

        for (int i = 0; i < KMEM_CACHE_COUNT; i++)
        {
            uint8_t *p = kmalloc (sizes[i]);
            if (!p)
            {
                kpanic ("kmalloc test: basic alloc returned NULL");
            }
            *p = (uint8_t)sizes[i];
            if (*p != (uint8_t)sizes[i])
            {
                kpanic ("kmalloc test: write/read failed");
            }
            pr_info ("kmalloc: size=%u at 0x%x ok\n", sizes[i], (uint32_t)p);
            kfree (p);
        }
        pr_info ("kmalloc: alloc/write/free for all 6 sizes passed\n");
    }

    /* 2. kfree(NULL) is a no-op */
    {
        kfree (NULL);
        pr_info ("kmalloc: kfree(NULL) is no-op\n");
    }

    /* 3. ksize returns the cache obj_size (round-up) */
    {
        void *p8 = kmalloc (1);   /* → cache 8   */
        void *p16 = kmalloc (9);  /* → cache 16  */
        void *p64 = kmalloc (33); /* → cache 64  */

        if (!p8 || !p16 || !p64)
        {
            kpanic ("kmalloc test: ksize alloc returned NULL");
        }
        if (ksize (p8) != KMEM_CACHE_8)
        {
            kpanic ("kmalloc test: ksize(1) != 8");
        }
        if (ksize (p16) != KMEM_CACHE_16)
        {
            kpanic ("kmalloc test: ksize(9) != 16");
        }
        if (ksize (p64) != KMEM_CACHE_64)
        {
            kpanic ("kmalloc test: ksize(33) != 64");
        }
        pr_info ("kmalloc: ksize round-up: 1->%u  9->%u  33->%u\n",
                 (uint32_t)ksize (p8), (uint32_t)ksize (p16),
                 (uint32_t)ksize (p64));
        kfree (p8);
        kfree (p16);
        kfree (p64);
    }

    /* 4. returned pointer falls within chunk pool zone */
    {
        void *p = kmalloc (32);
        if (!p)
        {
            kpanic ("kmalloc test: zone alloc returned NULL");
        }
        if ((uint32_t)p < KCPOOL_VIRT_BASE || (uint32_t)p >= KCPOOL_VIRT_MAX)
        {
            kpanic ("kmalloc test: pointer outside chunk pool zone");
        }
        pr_info ("kmalloc: pointer 0x%x within chunk pool zone\n", (uint32_t)p);
        kfree (p);
    }

    /* 5. refill: allocate more than one page worth of cache-8 objects */
    {
        uint32_t per_page = CPOOL_DATA_SIZE / KMEM_CACHE_8; /* 511 */
        uint32_t n = per_page + 10;                         /* span 2 pages */
        static void *ptrs[521];

        for (uint32_t i = 0; i < n; i++)
        {
            ptrs[i] = kmalloc (KMEM_CACHE_8);
            if (!ptrs[i])
            {
                kpanic ("kmalloc test: refill alloc returned NULL");
            }
        }
        pr_info ("kmalloc: refill: %u cache-8 objects across 2 pages\n", n);

        for (uint32_t i = 0; i < n; i++)
        {
            kfree (ptrs[i]);
        }
        pr_info ("kmalloc: refill: all freed\n");
    }

    /* 6. page return: full chunk pool page goes back to PMM when all objects
     * freed */
    {
        uint32_t n = CPOOL_DATA_SIZE / KMEM_CACHE_8; /* 511 */
        static void *ptrs[511];
        kmalloc_stats_t s;

        kmalloc_query (&s);
        uint32_t pages_before = s.pool_pages;

        for (uint32_t i = 0; i < n; i++)
        {
            ptrs[i] = kmalloc (KMEM_CACHE_8);
        }
        kmalloc_query (&s);
        if (s.pool_pages != pages_before + 1)
        {
            kpanic (
                "kmalloc test: expected +1 chunk pool page after full fill");
        }

        for (uint32_t i = 0; i < n; i++)
        {
            kfree (ptrs[i]);
        }
        kmalloc_query (&s);
        if (s.pool_pages != pages_before)
        {
            kpanic ("kmalloc test: chunk pool page not returned to PMM after "
                    "full free");
        }
        pr_info ("kmalloc: page return: %u -> %u -> %u chunk pool pages\n",
                 pages_before, pages_before + 1, pages_before);
    }

    pr_info ("kmalloc (chunk pool) test passed\n\n");
}
