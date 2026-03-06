#include "../include/paging.h"
#include "../include/phys_page_frame.h"
#include "../include/printk.h"

uint32_t page_directory[PD_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));
uint32_t first_page_table[PT_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));

static uint32_t pt_pool[PT_POOL_SIZE][PT_POOL_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));
static uint32_t pt_pool_next = 0;

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
       this maps the PD at 0xFFFFF000 and all PTs at 0xFFC00000+ */
    page_directory[1023] = ((uint32_t)page_directory) | (PAGE_PRESENT | PAGE_RW);
    load_page_directory ((uint32_t *)page_directory);
    enable_paging ();
    pr_info ("Paging enabled (identity mapped first 4 MiB)\n");
    printk("\n");
}

void *get_physaddr(void *virtualaddr)
{
    unsigned long pdindex = (unsigned long)virtualaddr >> 22;
    unsigned long ptindex = (unsigned long)virtualaddr >> 12 & 0x03FF;

    unsigned long *pd = (unsigned long *)0xFFFFF000;
    if (!(pd[pdindex] & 0x01))
    {
        return (void *)0;
    }

    unsigned long *pt = ((unsigned long *)0xFFC00000) + (0x400 * pdindex);
    if (!(pt[ptindex] & 0x01))
    {
        return (void *)0;
    }

    return (void *)((pt[ptindex] & ~0xFFF) + ((unsigned long)virtualaddr & 0xFFF));
}

void map_page(void *physaddr, void *virtualaddr, unsigned int flags)
{
    if (((unsigned long)physaddr & 0xFFF) || ((unsigned long)virtualaddr & 0xFFF))
    {
        return;
    }

    unsigned long pdindex = (unsigned long)virtualaddr >> 22;
    unsigned long ptindex = (unsigned long)virtualaddr >> 12 & 0x03FF;
    unsigned long *pd = (unsigned long *)0xFFFFF000;

    if (!(pd[pdindex] & 0x01))
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
        pd[pdindex] = ((unsigned long)new_pt) | (PAGE_PRESENT | PAGE_RW);
    }

    unsigned long *pt = ((unsigned long *)0xFFC00000) + (0x400 * pdindex);
    if (pt[ptindex] & 0x01)
    {
        pt[ptindex] = 0;
    }

    pt[ptindex] = ((unsigned long)physaddr) | (flags & 0xFFF) | 0x01;
    flush_tlb ((unsigned long)virtualaddr);
}

void unmap_page(void *virtualaddr)
{
    if ((unsigned long)virtualaddr & 0xFFF)
    {
        return;
    }

    unsigned long pdindex = (unsigned long)virtualaddr >> 22;
    unsigned long ptindex = (unsigned long)virtualaddr >> 12 & 0x03FF;

    unsigned long *pd = (unsigned long *)0xFFFFF000;
    if (!(pd[pdindex] & 0x01))
    {
        return;
    }

    unsigned long *pt = ((unsigned long *)0xFFC00000) + (0x400 * pdindex);
    if (!(pt[ptindex] & 0x01))
    {
        return;
    }

    pt[ptindex] = 0;
    flush_tlb ((unsigned long)virtualaddr);
}

void *
alloc_page (void *virtualaddr, unsigned int flags)
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
    uint32_t cr0;
    uint32_t cr3;
    volatile uint32_t *test_addr;
    uint32_t old;

    pr_info ("#### Paging check ####\n");
    // verify CR0 and CR3
    cr0 = read_cr0 ();
    cr3 = read_cr3 ();

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

    if (cr3 == (uint32_t)page_directory)
    {
        pr_info ("CR3: points to page directory (0x%x)\n", cr3);
    }
    else
    {
        pr_err ("CR3: expected 0x%x, got 0x%x\n",
            (uint32_t)page_directory, cr3);
    }

    // identity mapping read/write at 0x100000
    test_addr = (volatile uint32_t *)0x100000;
    old = *test_addr;
    *test_addr = 0xDEADBEEF;
    if (*test_addr == 0xDEADBEEF)
    {
        pr_info ("Identity map: read/write at 0x100000\n");
    }
    else
    {
        pr_err ("Identity map: read back wrong value at 0x100000\n");
    }
    *test_addr = old;

    pr_info ("Paging check passed\n");
}
