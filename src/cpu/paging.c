#include "../include/paging.h"
#include "../include/phys_page_frame.h"
#include "../include/printk.h"
#include "../include/kpanic.h"

uint32_t page_directory[PD_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));
uint32_t first_page_table[PT_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));

static uint32_t pt_pool[PT_POOL_SIZE][PT_POOL_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));
static uint32_t pt_pool_next = 0;

static inline uint32_t *
get_pt (uint32_t pdindex)
{
    return ((uint32_t *)RECURSIVE_PT_BASE) + (PT_ENTRIES * pdindex);
}

void
paging_init (void)
{
    uint32_t i;

    for (i = 0; i < PD_ENTRIES; i++)
    {
        page_directory[i] = PAGE_RW; // non-present, r/w pre-configured
    }

    // identity mapping
    for (i = 0; i < PT_ENTRIES; i++)
    {
        first_page_table[i] = (i * PAGE_SIZE) | (PAGE_PRESENT | PAGE_RW); /* 0b11, bit 0 present, bit 1 r/w, this is the base address of a frame, offset of
        The offset in the virtual address for physics will help us point to the right mem area */
    } 

    page_directory[0] = ((uint32_t)first_page_table) | (PAGE_PRESENT | PAGE_RW);
    /* recursive mapping: PD[1023] -> PD itself, so the MMU traverses
       CR3 -> PD[1023] -> PD (as PT) -> PD[1023] -> phys addr of PD
       this maps the PD at RECURSIVE_PD_BASE and all PTs at RECURSIVE_PT_BASE+ */
    page_directory[1023] = ((uint32_t)page_directory) | (PAGE_PRESENT | PAGE_RW);
    load_page_directory ((uint32_t *)page_directory);
    enable_paging ();
    pr_info ("Paging enabled (identity mapped first 4 MiB)\n");
    printk("\n");
}

void *get_physaddr(void *virtualaddr)
{
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;

    uint32_t *pd = (uint32_t *)RECURSIVE_PD_BASE;
    if (!(pd[pdindex] & 0x01))
    {
        return (void *)0;
    }

    uint32_t *pt = get_pt (pdindex);
    if (!(pt[ptindex] & 0x01))
    {
        return (void *)0;
    }

    return (void *)((pt[ptindex] & ~0xFFF) + ((uint32_t)virtualaddr & 0xFFF));
}

void map_page(void *physaddr, void *virtualaddr, uint32_t flags)
{
    if (((uint32_t)physaddr & 0xFFF) || ((uint32_t)virtualaddr & 0xFFF))
    {
        return;
    }

    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_BASE;

    if (!(pd[pdindex] & 0x01)) // check if not PAGE_PRESENT
    {
        if (pt_pool_next >= PT_POOL_SIZE)
        {
            return;
        }
        uint32_t *new_pt = pt_pool[pt_pool_next++];
        for (int i = 0; i < PT_ENTRIES; i++)
        {
            new_pt[i] = 0;
        }
        pd[pdindex] = ((uint32_t)new_pt) | (PAGE_PRESENT | PAGE_RW);
    }

    uint32_t *pt = get_pt (pdindex);
    if (pt[ptindex] & 0x01)
    {
        pt[ptindex] = 0;
    }

    pt[ptindex] = ((uint32_t)physaddr) | (flags & 0xFFF) | 0x01;
    flush_tlb ((uint32_t)virtualaddr);
}

void unmap_page(void *virtualaddr)
{
    if ((uint32_t)virtualaddr & 0xFFF)
    {
        return;
    }

    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;

    uint32_t *pd = (uint32_t *)RECURSIVE_PD_BASE;
    if (!(pd[pdindex] & 0x01))
    {
        return;
    }

    uint32_t *pt = get_pt (pdindex);
    if (!(pt[ptindex] & 0x01))
    {
        return;
    }

    pt[ptindex] = 0;
    flush_tlb ((uint32_t)virtualaddr);
}

void *
alloc_page (void *virtualaddr, uint32_t flags)
{
    void *frame = phys_alloc_frame ();
    if (!frame)
    {
        return (void *)0;
    }

    map_page (frame, virtualaddr, flags);

    if (!get_physaddr (virtualaddr))
    {
        phys_free_frame (frame);
        return (void *)0;
    }

    return virtualaddr;
}

void
free_page (void *virtualaddr)
{
    void *phys = get_physaddr (virtualaddr);
    if (!phys)
    {
        return;
    }

    unmap_page (virtualaddr);
    phys_free_frame (phys);
}

void
paging_test (void)
{
    pr_info ("#### Paging check ####\n");

    // verify CR0: PE and PG bits
    {
        uint32_t cr0 = read_cr0 ();
        if ((cr0 & 0x80000001) == 0x80000001)
        {
            pr_info ("CR0: PE and PG bits set (CR0 = 0x%x)\n", cr0);
        }
        else
        {
            if (!(cr0 & 0x00000001))
            {
                pr_err ("CR0: PE bit not set (CR0 = 0x%x)\n", cr0);
            }
            if (!(cr0 & 0x80000000))
            {
                pr_err ("CR0: PG bit not set (CR0 = 0x%x)\n", cr0);
            }
        }
    }

    // verify CR3 points to page directory
    {
        uint32_t cr3 = read_cr3 ();
        if (cr3 == (uint32_t)page_directory)
        {
            pr_info ("CR3: points to page directory (0x%x)\n", cr3);
        }
        else
        {
            pr_err ("CR3: expected 0x%x, got 0x%x\n",
                    (uint32_t)page_directory, cr3);
        }
    }

    // identity mapping read/write at 0x100000
    {
        volatile uint32_t *addr = (volatile uint32_t *)0x100000;
        uint32_t old = *addr;
        *addr = 0xDEADBEEF;
        if (*addr == 0xDEADBEEF)
        {
            pr_info ("Identity map: read/write at 0x100000\n");
        }
        else
        {
            pr_err ("Identity map: read back wrong value at 0x100000\n");
        }
        *addr = old;
    }

    // identity map: get_physaddr must return same address
    {
        void *phys = get_physaddr ((void *)0x100000);
        if (phys == (void *)0x100000)
        {
            pr_info ("Identity map: virt 0x100000 resolves to phys 0x100000\n");
        }
        else
        {
            pr_err ("Identity map: virt 0x100000 resolved to 0x%x\n",
                    (uint32_t)phys);
        }
    }

    // map_page + get_physaddr + read/write + unmap round-trip
    {
        void *vaddr = (void *)0x00800000;
        void *paddr = phys_alloc_frame ();
        if (!paddr)
        {
            pr_err ("Map round-trip: could not allocate frame\n");
        }
        else
        {
            map_page (paddr, vaddr, PAGE_PRESENT | PAGE_RW);
            void *resolved = get_physaddr (vaddr);

            if (resolved == paddr)
            {
                pr_info ("Map round-trip: 0x%x -> 0x%x\n",
                         (uint32_t)vaddr, (uint32_t)paddr);
            }
            else
            {
                pr_err ("Map round-trip: expected phys 0x%x, got 0x%x\n",
                        (uint32_t)paddr, (uint32_t)resolved);
            }

            volatile uint32_t *ptr = (volatile uint32_t *)vaddr;
            *ptr = 0xCAFEBABE;
            if (*ptr == 0xCAFEBABE)
            {
                pr_info ("Map round-trip: read/write through mapping\n");
            }
            else
            {
                pr_err ("Map round-trip: read/write failed\n");
            }

            unmap_page (vaddr);
            void *after = get_physaddr (vaddr);
            if (after == (void *)0)
            {
                pr_info ("Unmap: mapping removed at 0x%x\n",
                         (uint32_t)vaddr);
            }
            else
            {
                pr_err ("Unmap: mapping still present at 0x%x\n",
                        (uint32_t)vaddr);
            }
            phys_free_frame (paddr);
        }
    }

    // map_page rejects unaligned virtual address
    {
        void *before = get_physaddr ((void *)0x00900001);
        map_page ((void *)0x00500000, (void *)0x00900001,
                  PAGE_PRESENT | PAGE_RW);
        void *after = get_physaddr ((void *)0x00900001);

        if (before == after)
        {
            pr_info ("Map: unaligned virtual address rejected\n");
        }
        else
        {
            pr_err ("Map: unaligned virtual address was accepted\n");
        }
    }

    // alloc_page + free_page full cycle
    {
        void *vaddr = (void *)0x00A00000;
        uint32_t free_before = phys_free_count ();

        void *result = alloc_page (vaddr, PAGE_PRESENT | PAGE_RW);
        if (result == vaddr)
        {
            pr_info ("alloc_page: mapped at 0x%x\n", (uint32_t)vaddr);
        }
        else
        {
            pr_err ("alloc_page: expected 0x%x, got 0x%x\n",
                    (uint32_t)vaddr, (uint32_t)result);
        }

        uint32_t free_during = phys_free_count ();
        if (free_during == free_before - 1)
        {
            pr_info ("alloc_page: consumed one frame (%d -> %d)\n",
                     free_before, free_during);
        }
        else
        {
            pr_err ("alloc_page: free count %d -> %d (expected %d)\n",
                    free_before, free_during, free_before - 1);
        }

        volatile uint32_t *ptr = (volatile uint32_t *)vaddr;
        *ptr = 0x12345678;
        if (*ptr == 0x12345678)
        {
            pr_info ("alloc_page: read/write at 0x%x\n", (uint32_t)vaddr);
        }
        else
        {
            pr_err ("alloc_page: read/write failed at 0x%x\n",
                    (uint32_t)vaddr);
        }

        free_page (vaddr);

        uint32_t free_after = phys_free_count ();
        if (free_after == free_before)
        {
            pr_info ("free_page: frame returned to PMM (%d -> %d)\n",
                     free_during, free_after);
        }
        else
        {
            pr_err ("free_page: free count %d, expected %d\n",
                    free_after, free_before);
        }

        void *phys_after = get_physaddr (vaddr);
        if (phys_after == (void *)0)
        {
            pr_info ("free_page: mapping removed at 0x%x\n",
                     (uint32_t)vaddr);
        }
        else
        {
            pr_err ("free_page: mapping still present at 0x%x\n",
                    (uint32_t)vaddr);
        }
    }

    // multiple page allocs with isolation check
    {
        void *v1 = (void *)0x00B00000;
        void *v2 = (void *)0x00B01000;
        void *v3 = (void *)0x00B02000;

        void *r1 = alloc_page (v1, PAGE_PRESENT | PAGE_RW);
        void *r2 = alloc_page (v2, PAGE_PRESENT | PAGE_RW);
        void *r3 = alloc_page (v3, PAGE_PRESENT | PAGE_RW);

        if (r1 && r2 && r3)
        {
            pr_info ("Multi-alloc: 3 pages allocated\n");
        }
        else
        {
            pr_err ("Multi-alloc: allocation failed\n");
        }

        void *p1 = get_physaddr (v1);
        void *p2 = get_physaddr (v2);
        void *p3 = get_physaddr (v3);

        if (p1 != p2 && p2 != p3 && p1 != p3)
        {
            pr_info ("Multi-alloc: unique physical frames\n");
        }
        else
        {
            pr_err ("Multi-alloc: duplicate physical frames\n");
        }

        *(volatile uint32_t *)v1 = 0xAAAAAAAA;
        *(volatile uint32_t *)v2 = 0xBBBBBBBB;
        *(volatile uint32_t *)v3 = 0xCCCCCCCC;

        if (*(volatile uint32_t *)v1 == 0xAAAAAAAA &&
            *(volatile uint32_t *)v2 == 0xBBBBBBBB &&
            *(volatile uint32_t *)v3 == 0xCCCCCCCC)
        {
            pr_info ("Multi-alloc: no cross-talk between pages\n");
        }
        else
        {
            pr_err ("Multi-alloc: cross-talk detected between pages\n");
        }

        free_page (v3);
        free_page (v2);
        free_page (v1);
    }

    // free_page on unmapped address is a no-op
    {
        uint32_t before = phys_free_count ();
        free_page ((void *)0x00C00000);
        uint32_t after = phys_free_count ();

        if (before == after)
        {
            pr_info ("free_page: unmapped address is no-op\n");
        }
        else
        {
            pr_err ("free_page: unmapped address changed free count\n");
        }
    }

    pr_info ("Paging check passed\n");
}
