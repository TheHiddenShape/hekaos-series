#ifndef KMALLOC_H
#define KMALLOC_H

#include <stddef.h>
#include <stdint.h>

/* Kernel heap lives in the top 1 GiB, below the recursive-PT window */
// kmalloc zone: 0xC0000000 – 0xEFFFFFFF (vmalloc lives above at 0xF0000000)
#define KHEAP_VIRT_BASE 0xC0000000
#define KHEAP_VIRT_MAX 0xEFFFFFFF

void heap_init (void);
void *kmalloc (size_t size);
void kfree (void *ptr);
void *kbrk (uint32_t nbytes); /* nbytes=0 → query current end, else extend */
size_t ksize (void *ptr);

void kmalloc_test (void);

#endif
