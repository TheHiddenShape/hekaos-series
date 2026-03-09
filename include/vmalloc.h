#ifndef VMALLOC_H
#define VMALLOC_H

#include <stdint.h>

// vmalloc zone: above kmalloc heap (0xEFFFFFFF), below recursive-PT window
#define VMALLOC_BASE 0xF0000000
#define VMALLOC_MAX 0xFFBFFFFF

void vmalloc_init (void);
void *vmalloc (uint32_t npages);
void vfree (void *ptr, uint32_t npages);
void *vbrk (void);
uint32_t vsize (void);

void vmalloc_test (void);

#endif
