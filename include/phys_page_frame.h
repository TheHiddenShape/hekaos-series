#ifndef PHYS_PAGE_FRAME_H
#define PHYS_PAGE_FRAME_H

#include <stdint.h>

// bitmap-based frame allocator. https://wiki.osdev.org/Page_Frame_Allocation

// Total physical memory managed (64 MiB)
#define PHYS_MEM_SIZE     0x04000000

// Physical memory managed starts after identity-mapped region (4 MiB)
#define PHYS_REGION_START    0x00400000

#define PHYS_FRAME_SIZE      4096
#define PHYS_TOTAL_FRAMES    (PHYS_MEM_SIZE / PHYS_FRAME_SIZE)

// Number of frames available in the managed region
#define PHYS_REGION_FRAMES   ((PHYS_MEM_SIZE - PHYS_REGION_START) / PHYS_FRAME_SIZE)

void        phys_mem_init (void);
void       *phys_alloc_frame (void);
void        phys_free_frame (void *frame);
uint32_t    phys_free_count (void);
void        phys_mem_test (void);

#endif
