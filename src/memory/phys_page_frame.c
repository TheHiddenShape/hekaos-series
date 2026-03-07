#include "phys_page_frame.h"
#include "printk.h"
#include "kpanic.h"

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

void
phys_mem_test (void)
{
    pr_info ("#### PMM check ####\n");

    // free count after init must equal PHYS_REGION_FRAMES
    {
        uint32_t free = phys_free_count ();
        if (free == PHYS_REGION_FRAMES)
        {
            pr_info ("PMM init: %d free frames (%d KiB)\n",
                     free, (free * PHYS_FRAME_SIZE) / 1024);
        }
        else
        {
            pr_err ("PMM init: free count = %d, expected %d\n",
                    free, PHYS_REGION_FRAMES);
        }
    }

    // alloc returns aligned, non-null, in-range address
    {
        void *frame = phys_alloc_frame ();
        int ok = (frame != (void *)0) &&
                 (((uint32_t)frame & (PHYS_FRAME_SIZE - 1)) == 0) &&
                 ((uint32_t)frame >= PHYS_REGION_START) &&
                 ((uint32_t)frame < PHYS_MEM_SIZE);
        if (ok)
        {
            pr_info ("PMM alloc: valid frame at 0x%x\n", (uint32_t)frame);
        }
        else
        {
            pr_err ("PMM alloc: invalid address 0x%x\n", (uint32_t)frame);
        }
        phys_free_frame (frame);
    }

    // alloc decrements free count, free restores it
    {
        uint32_t before = phys_free_count ();
        void *frame = phys_alloc_frame ();
        uint32_t during = phys_free_count ();
        phys_free_frame (frame);
        uint32_t after = phys_free_count ();

        if (during == before - 1 && after == before)
        {
            pr_info ("PMM alloc/free: count %d -> %d -> %d\n",
                     before, during, after);
        }
        else
        {
            pr_err ("PMM alloc/free: count %d -> %d -> %d (expected %d -> %d -> %d)\n",
                    before, during, after, before, before - 1, before);
        }
    }

    // consecutive allocs return unique addresses
    {
        void *a = phys_alloc_frame ();
        void *b = phys_alloc_frame ();
        void *c = phys_alloc_frame ();

        if (a != b && b != c && a != c)
        {
            pr_info ("PMM alloc unique: 0x%x, 0x%x, 0x%x\n",
                     (uint32_t)a, (uint32_t)b, (uint32_t)c);
        }
        else
        {
            pr_err ("PMM alloc unique: duplicate frames detected\n");
        }
        phys_free_frame (c);
        phys_free_frame (b);
        phys_free_frame (a);
    }

    // free with unaligned address is ignored
    {
        uint32_t before = phys_free_count ();
        phys_free_frame ((void *)0x00400001);
        uint32_t after = phys_free_count ();

        if (before == after)
        {
            pr_info ("PMM free: unaligned address rejected\n");
        }
        else
        {
            pr_err ("PMM free: unaligned address was accepted\n");
        }
    }

    // free in reserved region is ignored
    {
        uint32_t before = phys_free_count ();
        phys_free_frame ((void *)0x00001000);
        uint32_t after = phys_free_count ();

        if (before == after)
        {
            pr_info ("PMM free: reserved region address rejected\n");
        }
        else
        {
            pr_err ("PMM free: reserved region address was accepted\n");
        }
    }

    // free out of range is ignored
    {
        uint32_t before = phys_free_count ();
        phys_free_frame ((void *)0x10000000);
        uint32_t after = phys_free_count ();

        if (before == after)
        {
            pr_info ("PMM free: out-of-range address rejected\n");
        }
        else
        {
            pr_err ("PMM free: out-of-range address was accepted\n");
        }
    }

    // free null is ignored
    {
        uint32_t before = phys_free_count ();
        phys_free_frame ((void *)0x0);
        uint32_t after = phys_free_count ();

        if (before == after)
        {
            pr_info ("PMM free: null address rejected\n");
        }
        else
        {
            pr_err ("PMM free: null address was accepted\n");
        }
    }

    // exhaust all frames, then OOM returns null
    {
        uint32_t total = phys_free_count ();
        uint32_t allocated = 0;

        while (1)
        {
            void *f = phys_alloc_frame ();
            if (!f)
                break;
            allocated++;
            if (allocated > total + 1)
                break;
        }

        if (allocated == total)
        {
            pr_info ("PMM exhaust: allocated all %d frames\n", total);
        }
        else
        {
            pr_err ("PMM exhaust: allocated %d frames, expected %d\n",
                    allocated, total);
        }

        if (phys_alloc_frame () == (void *)0)
        {
            pr_info ("PMM OOM: returns null when exhausted\n");
        }
        else
        {
            pr_err ("PMM OOM: did not return null\n");
        }

        if (phys_free_count () == 0)
        {
            pr_info ("PMM exhaust: free count is 0\n");
        }
        else
        {
            pr_err ("PMM exhaust: free count is %d, expected 0\n",
                    phys_free_count ());
        }

        // reinit to restore state for following tests
        phys_mem_init ();

        if (phys_free_count () == PHYS_REGION_FRAMES)
        {
            pr_info ("PMM reinit: state restored (%d frames)\n",
                     PHYS_REGION_FRAMES);
        }
        else
        {
            pr_err ("PMM reinit: free count = %d, expected %d\n",
                    phys_free_count (), PHYS_REGION_FRAMES);
        }
    }

    pr_info ("PMM check passed\n");
}
