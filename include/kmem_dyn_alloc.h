#ifndef KMEM_DYN_ALLOC_H
#define KMEM_DYN_ALLOC_H

#include <stddef.h>
#include <stdint.h>

/* general-purpose kernel allocator: byte-precise, 8-byte aligned,
 * doubly-linked free-list with first-fit and bi-directional coalescing.
 * virtual heap: 0xC0000000 – 0xCFFFFFFF (chunk pool zone lives above at
 * 0xD0000000)
 */

#define KHEAP_VIRT_BASE 0xC0000000
#define KHEAP_VIRT_MAX 0xCFFFFFFF

void kmem_dyn_alloc_init (void);
void *kmem_alloc (size_t size);
void kmem_free (void *addr);
void *kmem_brk (uint32_t nbytes); /* nbytes=0 → query current end */
size_t kmem_size (void *addr);

typedef struct
{
    uint32_t heap_end;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t used_bytes;
    uint32_t free_bytes;
} kmem_stats_t;

void kmem_dyn_alloc_test (void);
void kmem_dyn_alloc_query (kmem_stats_t *stats);

#endif
