#include "phys_page_frame.h"
#include "printk.h"

// Bitmap: 1 bit per frame. 0 = free, 1 = used. PHYS_TOTAL_FRAMES / 32 gives the number of uint32_t words needed.

#define BITMAP_SIZE (PHYS_TOTAL_FRAMES / 32)

static uint32_t frame_bitmap[BITMAP_SIZE];

static inline void
bitmap_set (uint32_t frame)
{
    frame_bitmap[frame / 32] |= (1 << (frame % 32));
}

static inline void
bitmap_clear (uint32_t frame)
{
    frame_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

static inline int
bitmap_test (uint32_t frame)
{
    return frame_bitmap[frame / 32] & (1 << (frame % 32));
}

void
phys_mem_init (void)
{
    uint32_t i;
    uint32_t reserved_frames;

    for (i = 0; i < BITMAP_SIZE; i++)
    {
        frame_bitmap[i] = 0xFFFFFFFF;
    }

    reserved_frames = PHYS_REGION_START / PHYS_FRAME_SIZE;
    for (i = reserved_frames; i < PHYS_TOTAL_FRAMES; i++)
    {
        bitmap_clear (i);
    }

    pr_info ("PMM initialized: %d frames free (%d KiB)\n",
             PHYS_REGION_FRAMES, (PHYS_REGION_FRAMES * PHYS_FRAME_SIZE) / 1024);
}

void *
phys_alloc_frame (void)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < BITMAP_SIZE; i++)
    {
        if (frame_bitmap[i] == 0xFFFFFFFF)
        {
            continue; // all 32 frames in this word are used */
        }

        for (j = 0; j < 32; j++)
        {
            uint32_t frame = i * 32 + j;
            if (frame >= PHYS_TOTAL_FRAMES)
            {
                return (void *)0;
            }
            if (!bitmap_test (frame))
            {
                bitmap_set (frame);
                return (void *)(frame * PHYS_FRAME_SIZE);
            }
        }
    }

    return (void *)0;
}

void
phys_free_frame (void *frame)
{
    uint32_t addr = (uint32_t)frame;

    if (addr & (PHYS_FRAME_SIZE - 1))
    {
        return; // not aligned
    }

    uint32_t index = addr / PHYS_FRAME_SIZE;
    if (index >= PHYS_TOTAL_FRAMES || index < (PHYS_REGION_START / PHYS_FRAME_SIZE))
    {
        return; // out of range or reserved region
    }

    bitmap_clear (index);
}

uint32_t
phys_free_count (void)
{
    uint32_t count = 0;
    uint32_t i;

    for (i = 0; i < PHYS_TOTAL_FRAMES; i++)
    {
        if (!bitmap_test (i))
        {
            count++;
        }
    }

    return count;
}
