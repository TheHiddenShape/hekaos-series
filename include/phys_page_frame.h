#ifndef PHYS_PAGE_FRAME_H
#define PHYS_PAGE_FRAME_H

#include <stdint.h>

// bitmap-based frame allocator. https://wiki.osdev.org/Page_Frame_Allocation

// Total physical memory managed (16 MiB)
#define PMM_MEMORY_SIZE     0x01000000

// Physical memory managed starts after identity-mapped region (4 MiB)
#define PMM_REGION_START    0x00400000

#define PMM_FRAME_SIZE      4096
#define PMM_TOTAL_FRAMES    (PMM_MEMORY_SIZE / PMM_FRAME_SIZE)

// Number of frames available in the managed region
#define PMM_REGION_FRAMES   ((PMM_MEMORY_SIZE - PMM_REGION_START) / PMM_FRAME_SIZE)

void        pmm_init (void);
void       *pmm_alloc_frame (void);
void        pmm_free_frame (void *frame);
uint32_t    pmm_free_count (void);

#endif
