#include "../include/paging.h"
#include "../include/printk.h"

uint32_t page_directory[1024] __attribute__ ((aligned (4096)));
uint32_t first_page_table[1024] __attribute__ ((aligned (4096)));

void
paging_init (void)
{
    uint32_t i;

    for (i = 0; i < 1024; i++)
    {
        page_directory[i] = 0x00000002;
    }
    for (i = 0; i < 1024; i++)
    {
        first_page_table[i] = (i * 0x1000) | 3;
    }

    page_directory[0] = ((uint32_t)first_page_table) | 3;
    load_page_directory ((uint32_t *)page_directory);
    enable_paging ();
    pr_info ("Paging enabled (identity mapped first 4 MiB)\n");
    printk("\n");
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
