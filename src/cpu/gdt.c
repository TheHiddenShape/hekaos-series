#include "gdt.h"
#include "klib.h"
#include "printk.h"
#include "tss.h"

#define GDT_ENTRIES 8

struct gdt_entry gdt[GDT_ENTRIES] __attribute__ ((section (
    ".gdt"))); /* tells the compiler: "put this variable in the .gdt section" */
struct gdt_ptr gp;

extern void gdt_flush (uint32_t);

void
gdt_set_gate (int num, uint32_t base, uint32_t limit, uint8_t access,
              uint8_t gran)
{
    if (limit > 0xFFFFF)
    {
        pr_err ("GDT: limit > 0xFFFFF is forbidden\n");
    }

    gdt[num].base_low = (base & 0xFFFF);        /* 16 low bits */
    gdt[num].base_middle = (base >> 16) & 0xFF; /* bits 16-23 */
    gdt[num].base_high = (base >> 24) & 0xFF;   /* 24-31 high bits */

    gdt[num].limit_low = (limit & 0xFFFF); /* 16 low bits */

    gdt[num].granularity = (limit >> 16) & 0x0F; /* bits 16-19 */
    gdt[num].granularity |= gran & 0xF0;

    gdt[num].access = access;
}

void
gdt_init (void)
{
    gp.limit = (sizeof (struct gdt_entry) * GDT_ENTRIES) - 1;
    gp.base = (uint32_t)&gdt;

    gdt_set_gate (0, 0, 0, 0, 0); /* null segment */

    /* ring 0, kernel segments, full CPU privileges */
    gdt_set_gate (1, 0x00000000, 0x000FFFFF, 0x9A,
                  0xCF); /* kernel code segment */
    gdt_set_gate (2, 0x00000000, 0x000FFFFF, 0x92,
                  0xCF); /* kernel data segment */
    gdt_set_gate (3, 0x00000000, 0x000FFFFF, 0x92,
                  0xCF); /* kernel stack segment */

    /* ring 3, user segments, restricted privileges */
    gdt_set_gate (4, 0x00000000, 0x000FFFFF, 0xFA,
                  0xCF); /* user mode code segment */
    gdt_set_gate (5, 0x00000000, 0x000FFFFF, 0xF2,
                  0xCF); /* user mode data segment */
    gdt_set_gate (6, 0x00000000, 0x000FFFFF, 0xF2,
                  0xCF); /* user mode stack segment */

    /* TSS descriptor. access=0x89: present, 32-bit available TSS.
     * gran=0x00: byte granularity (no 4K, no D/B). Selector = 7<<3 = 0x38. */
    gdt_set_gate (7, (uint32_t)&tss, sizeof (tss) - 1, 0x89, 0x00);

    /* load gdt into gdtr */
    gdt_flush ((uint32_t)&gp);
}

uint32_t
gdt_get_base_address (void)
{
    return (uint32_t)&gdt;
}

void
klog_gdt_info (void)
{
    pr_info ("#### GDT Information ####\n");
    pr_info ("GDT Base Address: %p\n", (void *)&gdt);
    pr_info ("Expected Address: 0x800\n");
    pr_info ("GDT Entries:      %d\n", GDT_ENTRIES);
    pr_info ("GDT Size:         %d bytes\n",
             (int)(sizeof (struct gdt_entry) * GDT_ENTRIES));

    if ((uint32_t)&gdt == 0x800)
    {
        pr_info ("Status:           [OK] GDT is at correct address!\n");
    }
    else
    {
        pr_warn ("Status:           [WARN] GDT address mismatch!\n");
    }
    printk ("\n");
}
