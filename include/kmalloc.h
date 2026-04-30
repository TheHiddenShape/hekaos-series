#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>
#include <stdint.h>

/* chunk pool zone: 0xD0000000 – 0xEFFFFFFF (512 MiB)
 * kmem_dyn_alloc heap lives below at 0xC0000000 – 0xCFFFFFFF */
#define KCPOOL_VIRT_BASE 0xD0000000
#define KCPOOL_VIRT_MAX 0xEFFFFFFF

enum kmem_cache_size
{
    KMEM_CACHE_8 = 8,
    KMEM_CACHE_16 = 16,
    KMEM_CACHE_32 = 32,
    KMEM_CACHE_64 = 64,
    KMEM_CACHE_128 = 128,
    KMEM_CACHE_256 = 256,
};

#define KMEM_CACHE_COUNT 6

void kmalloc_init (void);
void *kmalloc (size_t size);
void kfree (void *addr);
size_t ksize (void *addr);

typedef struct
{
    uint32_t pool_pages;
    uint32_t free_objects;
} kmalloc_stats_t;

void kmalloc_test (void);
void kmalloc_query (kmalloc_stats_t *stats);

#endif
