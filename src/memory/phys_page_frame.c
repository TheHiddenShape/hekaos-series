#include "phys_page_frame.h"
#include "kpanic.h"
#include "printk.h"

/* total physical memory managed (64 MiB) */
#define PHYS_MEM_SIZE 0x04000000
/* physical memory managed starts after identity-mapped region (4 MiB) */
#define PHYS_REGION_START 0x00400000
#define PHYS_FRAME_SIZE 4096
#define PHYS_TOTAL_FRAMES (PHYS_MEM_SIZE / PHYS_FRAME_SIZE)
/* number of frames available in the managed region */
#define PHYS_REGION_FRAMES                                                     \
    ((PHYS_MEM_SIZE - PHYS_REGION_START) / PHYS_FRAME_SIZE)

/* bitmap: 1 bit per frame. 0 = free, 1 = used. PHYS_TOTAL_FRAMES / 32 gives the
 * number of uint32_t words needed. */

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

    pr_info ("PMM initialized: %d frames free (%d KiB)\n", PHYS_REGION_FRAMES,
             (PHYS_REGION_FRAMES * PHYS_FRAME_SIZE) / 1024);
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
            continue; /* all 32 frames in this word are used */
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
        return; /* not aligned */
    }

    uint32_t index = addr / PHYS_FRAME_SIZE;
    if (index >= PHYS_TOTAL_FRAMES
        || index < (PHYS_REGION_START / PHYS_FRAME_SIZE))
    {
        return; /* out of range or reserved region */
    }

    bitmap_clear (index);
}

void *
phys_alloc_contiguous (uint32_t n_frames)
{
    if (n_frames == 0)
    {
        return (void *)0;
    }

    uint32_t count = 0;
    uint32_t start = 0;
    uint32_t i;

    for (i = 0; i < PHYS_TOTAL_FRAMES; i++)
    {
        if (!bitmap_test (i))
        {
            if (count == 0)
            {
                start = i;
            }
            if (++count == n_frames)
            {
                for (uint32_t j = start; j < start + n_frames; j++)
                {
                    bitmap_set (j);
                }
                return (void *)(start * PHYS_FRAME_SIZE);
            }
        }
        else
        {
            count = 0;
        }
    }

    return (void *)0;
}

void
phys_free_contiguous (void *base, uint32_t n_frames)
{
    uint32_t addr = (uint32_t)base;
    uint32_t reserved = PHYS_REGION_START / PHYS_FRAME_SIZE;

    if (n_frames == 0 || (addr & (PHYS_FRAME_SIZE - 1)))
    {
        return;
    }

    uint32_t start = addr / PHYS_FRAME_SIZE;

    if (start < reserved || n_frames > PHYS_TOTAL_FRAMES - start)
    {
        return;
    }

    for (uint32_t i = start; i < start + n_frames; i++)
    {
        bitmap_clear (i);
    }
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

    /* free count after init must equal PHYS_REGION_FRAMES */
    {
        uint32_t free = phys_free_count ();
        if (free == PHYS_REGION_FRAMES)
        {
            pr_info ("PMM init: %d free frames (%d KiB)\n", free,
                     (free * PHYS_FRAME_SIZE) / 1024);
        }
        else
        {
            kpanic ("PMM init: free count mismatch");
        }
    }

    /* alloc returns aligned, non-null, in-range address */
    {
        void *frame = phys_alloc_frame ();
        int ok = (frame != (void *)0)
                 && (((uint32_t)frame & (PHYS_FRAME_SIZE - 1)) == 0)
                 && ((uint32_t)frame >= PHYS_REGION_START)
                 && ((uint32_t)frame < PHYS_MEM_SIZE);
        if (ok)
        {
            pr_info ("PMM alloc: valid frame at 0x%x\n", (uint32_t)frame);
        }
        else
        {
            kpanic ("PMM alloc: invalid frame address");
        }
        phys_free_frame (frame);
    }

    /* alloc decrements free count, free restores it */
    {
        uint32_t before = phys_free_count ();
        void *frame = phys_alloc_frame ();
        uint32_t during = phys_free_count ();
        phys_free_frame (frame);
        uint32_t after = phys_free_count ();

        if (during == before - 1 && after == before)
        {
            pr_info ("PMM alloc/free: count %d -> %d -> %d\n", before, during,
                     after);
        }
        else
        {
            kpanic ("PMM alloc/free: free count inconsistent");
        }
    }

    /* consecutive allocs return unique addresses */
    {
        void *a = phys_alloc_frame ();
        void *b = phys_alloc_frame ();
        void *c = phys_alloc_frame ();

        if (a != b && b != c && a != c)
        {
            pr_info ("PMM alloc unique: 0x%x, 0x%x, 0x%x\n", (uint32_t)a,
                     (uint32_t)b, (uint32_t)c);
        }
        else
        {
            kpanic ("PMM alloc unique: duplicate frames detected");
        }
        phys_free_frame (c);
        phys_free_frame (b);
        phys_free_frame (a);
    }

    /* free with unaligned address is ignored */
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
            kpanic ("PMM free: unaligned address was accepted");
        }
    }

    /* free in reserved region is ignored */
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
            kpanic ("PMM free: reserved region address was accepted");
        }
    }

    /* free out of range is ignored */
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
            kpanic ("PMM free: out-of-range address was accepted");
        }
    }

    /* free null is ignored */
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
            kpanic ("PMM free: null address was accepted");
        }
    }

    /* exhaust all frames, then OOM returns null */
    {
        uint32_t total = phys_free_count ();
        uint32_t allocated = 0;

        while (1)
        {
            void *f = phys_alloc_frame ();
            if (!f)
            {
                break;
            }
            allocated++;
            if (allocated > total + 1)
            {
                break;
            }
        }

        if (allocated == total)
        {
            pr_info ("PMM exhaust: allocated all %d frames\n", total);
        }
        else
        {
            kpanic ("PMM exhaust: frame count mismatch");
        }

        if (phys_alloc_frame () == (void *)0)
        {
            pr_info ("PMM OOM: returns null when exhausted\n");
        }
        else
        {
            kpanic ("PMM OOM: did not return null");
        }

        if (phys_free_count () == 0)
        {
            pr_info ("PMM exhaust: free count is 0\n");
        }
        else
        {
            kpanic ("PMM exhaust: free count is not 0");
        }

        /* reinit to restore state for following tests */
        phys_mem_init ();

        if (phys_free_count () == PHYS_REGION_FRAMES)
        {
            pr_info ("PMM reinit: state restored (%d frames)\n",
                     PHYS_REGION_FRAMES);
        }
        else
        {
            kpanic ("PMM reinit: state not restored");
        }
    }

    /* phys_alloc_contiguous: n=1 behaves like phys_alloc_frame */
    {
        uint32_t before = phys_free_count ();
        void *base = phys_alloc_contiguous (1);
        uint32_t after = phys_free_count ();

        int ok = base != (void *)0
                 && ((uint32_t)base & (PHYS_FRAME_SIZE - 1)) == 0
                 && (uint32_t)base >= PHYS_REGION_START
                 && (uint32_t)base < PHYS_MEM_SIZE && after == before - 1;
        if (ok)
        {
            pr_info ("PMM contiguous n=1: valid frame at 0x%x\n",
                     (uint32_t)base);
        }
        else
        {
            kpanic ("PMM contiguous n=1: invalid result");
        }

        phys_free_contiguous (base, 1);
        if (phys_free_count () == before)
        {
            pr_info ("PMM contiguous n=1: free count restored\n");
        }
        else
        {
            kpanic ("PMM contiguous n=1: free count not restored");
        }
    }

    /* phys_alloc_contiguous: n=8 returns 8 physically adjacent frames */
    {
        uint32_t before = phys_free_count ();
        void *base = phys_alloc_contiguous (8);
        uint32_t after = phys_free_count ();

        int ok = base != (void *)0
                 && ((uint32_t)base & (PHYS_FRAME_SIZE - 1)) == 0
                 && (uint32_t)base >= PHYS_REGION_START && after == before - 8;
        if (ok)
        {
            pr_info ("PMM contiguous n=8: base at 0x%x, count %d -> %d\n",
                     (uint32_t)base, before, after);
        }
        else
        {
            kpanic ("PMM contiguous n=8: invalid result");
        }

        /* verify all 8 frames are marked used in the bitmap */
        uint32_t start_frame = (uint32_t)base / PHYS_FRAME_SIZE;
        int all_set = 1;
        for (uint32_t k = start_frame; k < start_frame + 8; k++)
        {
            if (!bitmap_test (k))
            {
                all_set = 0;
                break;
            }
        }
        if (all_set)
        {
            pr_info ("PMM contiguous n=8: all 8 frames marked used\n");
        }
        else
        {
            kpanic ("PMM contiguous n=8: bitmap inconsistency");
        }

        phys_free_contiguous (base, 8);
        if (phys_free_count () == before)
        {
            pr_info ("PMM contiguous n=8: free count restored\n");
        }
        else
        {
            kpanic ("PMM contiguous n=8: free count not restored after free");
        }
    }

    /* phys_alloc_contiguous: n=0 returns NULL */
    {
        void *base = phys_alloc_contiguous (0);
        if (base == (void *)0)
        {
            pr_info ("PMM contiguous n=0: returns null\n");
        }
        else
        {
            kpanic ("PMM contiguous n=0: expected null");
        }
    }

    /* phys_alloc_contiguous: n > available returns NULL */
    {
        void *base = phys_alloc_contiguous (PHYS_TOTAL_FRAMES + 1);
        if (base == (void *)0)
        {
            pr_info ("PMM contiguous n>max: returns null\n");
        }
        else
        {
            kpanic ("PMM contiguous n>max: expected null");
        }
    }

    /* phys_alloc_contiguous: used frame breaks a run, must skip to next valid
     * run */
    {
        /* after reinit: frames 1024+ are free (first-fit order guaranteed)
         * alloc a, b, c sequentially → frames 1024, 1025, 1026
         * free b (frame 1025) → hole:
         * [1024=used][1025=free][1026=used][1027+=free] alloc_contiguous(2):
         * run [1025] is length 1, insufficient → skip next run starts at 1027,
         * length >= 2 → return 0x403000 */
        void *a = phys_alloc_frame ();
        void *b = phys_alloc_frame ();
        void *c = phys_alloc_frame ();
        phys_free_frame (b);

        uint32_t before = phys_free_count ();
        void *run = phys_alloc_contiguous (2);

        int ok = run != (void *)0
                 && ((uint32_t)run & (PHYS_FRAME_SIZE - 1)) == 0
                 && (uint32_t)run >= (uint32_t)c + PHYS_FRAME_SIZE
                 && before - phys_free_count () == 2;
        if (ok)
        {
            pr_info ("PMM contiguous frag: found run at 0x%x, skipped hole at "
                     "0x%x\n",
                     (uint32_t)run, (uint32_t)b);
        }
        else
        {
            kpanic ("PMM contiguous frag: fragmentation handling failed");
        }

        /* verify the two allocated frames are consecutive in the bitmap */
        uint32_t sf = (uint32_t)run / PHYS_FRAME_SIZE;
        if (bitmap_test (sf) && bitmap_test (sf + 1))
        {
            pr_info (
                "PMM contiguous frag: both frames correctly marked used\n");
        }
        else
        {
            kpanic ("PMM contiguous frag: bitmap inconsistency after skip");
        }

        phys_free_contiguous (run, 2);
        phys_free_frame (a);
        phys_free_frame (c);
    }

    /* phys_free_contiguous: unaligned base is ignored */
    {
        uint32_t before = phys_free_count ();
        phys_free_contiguous ((void *)0x00400001, 1);
        if (phys_free_count () == before)
        {
            pr_info ("PMM free_contiguous: unaligned base rejected\n");
        }
        else
        {
            kpanic ("PMM free_contiguous: unaligned base was accepted");
        }
    }

    /* phys_free_contiguous: reserved region base is ignored */
    {
        uint32_t before = phys_free_count ();
        phys_free_contiguous ((void *)0x00001000, 1);
        if (phys_free_count () == before)
        {
            pr_info ("PMM free_contiguous: reserved base rejected\n");
        }
        else
        {
            kpanic ("PMM free_contiguous: reserved base was accepted");
        }
    }

    pr_info ("PMM check passed\n");
}
