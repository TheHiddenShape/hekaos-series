#include "paging.h"
#include "kpanic.h"
#include "phys_page_frame.h"
#include "printk.h"
#include "task.h"

#define PD_ENTRIES 1024
#define PT_ENTRIES 1024
#define RECURSIVE_PT_BASE 0xFFC00000
#define RECURSIVE_PD_BASE 0xFFFFF000

uint32_t page_directories[MAX_PROC][PD_ENTRIES]
    __attribute__ ((aligned (PAGE_SIZE)));
uint32_t first_page_table[PT_ENTRIES] __attribute__ ((aligned (PAGE_SIZE)));

static uint32_t current_pid = 0;

static inline uint32_t *
get_pt (uint32_t pdindex)
{
    return ((uint32_t *)RECURSIVE_PT_BASE) + (PT_ENTRIES * pdindex);
}

void
paging_init (void)
{
    uint32_t i;
    uint32_t *pd = page_directories[0];

    for (i = 0; i < PD_ENTRIES; i++)
    {
        pd[i] = PAGE_RW;
    }

    /*
     * identity mapping: each entry stores the physical base address of its
     * frame (i * PAGE_SIZE) with present and r/w bits set. the virtual-address
     * page offset is added by the MMU at access time to reach the right byte.
     */
    for (i = 0; i < PT_ENTRIES; i++)
    {
        first_page_table[i] = (i * PAGE_SIZE) | (PAGE_PRESENT | PAGE_RW);
    }

    pd[0] = ((uint32_t)first_page_table) | (PAGE_PRESENT | PAGE_RW);
    /*
     * recursive mapping: PD[1023] -> PD itself, so the MMU traverses
     * CR3 -> PD[1023] -> PD (as PT) -> PD[1023] -> phys addr of PD.
     * this maps the PD at RECURSIVE_PD_BASE and all PTs at RECURSIVE_PT_BASE+
     */
    pd[1023] = ((uint32_t)pd) | (PAGE_PRESENT | PAGE_RW);
    load_page_directory ((uint32_t *)pd);
    enable_paging ();
    pr_info ("paging enabled (identity mapped first 4 MiB)\n");
    printk ("\n");
}

void *
get_physaddr (void *virtualaddr)
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

void
map_page (void *physaddr, void *virtualaddr, uint32_t flags)
{
    if (((uint32_t)physaddr & 0xFFF) || ((uint32_t)virtualaddr & 0xFFF))
    {
        return;
    }

    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
    uint32_t *pd = (uint32_t *)RECURSIVE_PD_BASE;

    if (!(pd[pdindex] & 0x01))
    {
        uint32_t *new_pt = phys_alloc_frame ();
        if (!new_pt)
        {
            return;
        }
        /* propagate USER from the page's flags up to the PDE: the MMU requires
         * the U/S bit on BOTH levels for a Ring 3 access to be granted. */
        pd[pdindex] = ((uint32_t)new_pt)
                      | (PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER));
        flush_tlb ((uint32_t)get_pt (pdindex));
        uint32_t *pt_virt = get_pt (pdindex);
        for (int i = 0; i < PT_ENTRIES; i++)
        {
            pt_virt[i] = 0;
        }
    }

    uint32_t *pt = get_pt (pdindex);
    if (pt[ptindex] & 0x01)
    {
        pt[ptindex] = 0;
    }

    pt[ptindex] = ((uint32_t)physaddr) | (flags & 0xFFF) | 0x01;
    flush_tlb ((uint32_t)virtualaddr);
}

void
unmap_page (void *virtualaddr)
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

uint32_t
paging_proc_init (uint32_t pid)
{
    uint32_t i;
    uint32_t *pd = page_directories[pid];
    uint32_t *pd0 = page_directories[0];

    for (i = 0; i < PD_ENTRIES; i++)
    {
        pd[i] = PAGE_RW;
    }

    pd[0] = pd0[0];

    for (i = 768; i < 1023; i++)
    {
        pd[i] = pd0[i];
    }

    pd[1023] = ((uint32_t)pd) | (PAGE_PRESENT | PAGE_RW);

    /* kernel is identity-mapped: virtual == physical for static kernel data */
    return (uint32_t)pd;
}

void
paging_proc_switch (uint32_t pid)
{
    current_pid = pid;
    load_page_directory ((uint32_t *)page_directories[pid]);
}

/* scratch VAs in the kstack PDE (956), past every per-pid kstack slot. Used
 * transiently by fork/teardown to walk a non-current pgdir's PT contents. */
#define FORK_SCRATCH_PT_VA 0xEF200000u
#define FORK_SCRATCH_DATA_VA 0xEF201000u
#define KSTACK_PDE_INDEX (0xEF000000u >> 22)

int
paging_fork_copy (uint32_t child_pid)
{
    uint32_t *parent_pd = (uint32_t *)RECURSIVE_PD_BASE;
    uint32_t *child_pd = page_directories[child_pid];

    /* re-sync the kstack PDE from the live parent (defensive vs. pd0 snapshot)
     */
    child_pd[KSTACK_PDE_INDEX] = parent_pd[KSTACK_PDE_INDEX];

    /* user-space deep copy: PDE 1..767 (0=identity, 768..1022=kernel,
     * 1023=recursive) */
    for (uint32_t pdi = 1; pdi < 768; pdi++)
    {
        uint32_t parent_pde = parent_pd[pdi];
        if (!(parent_pde & PAGE_PRESENT))
        {
            continue;
        }

        void *child_pt_phys = phys_alloc_frame ();
        if (!child_pt_phys)
        {
            return -1;
        }

        map_page (child_pt_phys, (void *)FORK_SCRATCH_PT_VA,
                  PAGE_PRESENT | PAGE_RW);
        uint32_t *child_pt = (uint32_t *)FORK_SCRATCH_PT_VA;
        for (uint32_t i = 0; i < PT_ENTRIES; i++)
        {
            child_pt[i] = 0;
        }

        child_pd[pdi] = ((uint32_t)child_pt_phys) | (parent_pde & 0xFFF);

        uint32_t *parent_pt = get_pt (pdi);
        for (uint32_t pti = 0; pti < PT_ENTRIES; pti++)
        {
            uint32_t parent_pte = parent_pt[pti];
            if (!(parent_pte & PAGE_PRESENT))
            {
                continue;
            }

            void *child_frame = phys_alloc_frame ();
            if (!child_frame)
            {
                unmap_page ((void *)FORK_SCRATCH_PT_VA);
                return -1;
            }

            map_page (child_frame, (void *)FORK_SCRATCH_DATA_VA,
                      PAGE_PRESENT | PAGE_RW);
            uint32_t src_va = (pdi << 22) | (pti << 12);
            uint8_t *src = (uint8_t *)src_va;
            uint8_t *dst = (uint8_t *)FORK_SCRATCH_DATA_VA;
            for (uint32_t b = 0; b < PAGE_SIZE; b++)
            {
                dst[b] = src[b];
            }

            child_pt[pti] = ((uint32_t)child_frame) | (parent_pte & 0xFFF);
        }
    }

    unmap_page ((void *)FORK_SCRATCH_PT_VA);
    unmap_page ((void *)FORK_SCRATCH_DATA_VA);
    return 0;
}

/* Map one PAGE_USER page at `va` in pid's pgdir, then either copy `copy_size`
 * bytes from `src` into it (rest zeroed) or zero the whole page when src=NULL.
 * Creates the PDE on demand with PAGE_USER propagated. Returns 0 / -1 on OOM.
 */
static int
load_user_page (uint32_t pid, uint32_t va, const void *src, uint32_t copy_size)
{
    uint32_t *pd = page_directories[pid];
    uint32_t pdi = va >> 22;
    uint32_t pti = (va >> 12) & 0x3FFu;
    uint32_t pt_phys;

    if (!(pd[pdi] & PAGE_PRESENT))
    {
        void *new_pt = phys_alloc_frame ();
        if (!new_pt)
        {
            return -1;
        }
        pt_phys = (uint32_t)new_pt;
        map_page (new_pt, (void *)FORK_SCRATCH_PT_VA, PAGE_PRESENT | PAGE_RW);
        uint32_t *pt0 = (uint32_t *)FORK_SCRATCH_PT_VA;
        for (uint32_t i = 0; i < PT_ENTRIES; i++)
        {
            pt0[i] = 0;
        }
        unmap_page ((void *)FORK_SCRATCH_PT_VA);
        pd[pdi] = pt_phys | (PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }
    else
    {
        pt_phys = pd[pdi] & ~0xFFFu;
    }

    void *frame = phys_alloc_frame ();
    if (!frame)
    {
        return -1;
    }

    map_page ((void *)pt_phys, (void *)FORK_SCRATCH_PT_VA,
              PAGE_PRESENT | PAGE_RW);
    uint32_t *pt = (uint32_t *)FORK_SCRATCH_PT_VA;
    pt[pti] = ((uint32_t)frame) | (PAGE_PRESENT | PAGE_RW | PAGE_USER);
    unmap_page ((void *)FORK_SCRATCH_PT_VA);

    map_page (frame, (void *)FORK_SCRATCH_DATA_VA, PAGE_PRESENT | PAGE_RW);
    uint8_t *dst = (uint8_t *)FORK_SCRATCH_DATA_VA;
    if (src != NULL && copy_size > 0)
    {
        const uint8_t *s = (const uint8_t *)src;
        for (uint32_t b = 0; b < copy_size; b++)
        {
            dst[b] = s[b];
        }
        for (uint32_t b = copy_size; b < PAGE_SIZE; b++)
        {
            dst[b] = 0;
        }
    }
    else
    {
        for (uint32_t b = 0; b < PAGE_SIZE; b++)
        {
            dst[b] = 0;
        }
    }
    unmap_page ((void *)FORK_SCRATCH_DATA_VA);

    return 0;
}

int
paging_load_user_image (uint32_t pid, const void *src, uint32_t size)
{
    if (size == 0 || size > PAGE_SIZE)
    {
        return -1;
    }

    if (load_user_page (pid, USER_CODE_BASE, src, size) != 0)
    {
        return -1;
    }
    /* one stack page just below KERNEL_VIRT_BASE; user_esp starts at the very
     * top (KERNEL_VIRT_BASE), first user instruction must `sub` before any
     * deref so it lands inside [TOP - PAGE_SIZE, TOP). */
    if (load_user_page (pid, USER_STACK_TOP - PAGE_SIZE, NULL, 0) != 0)
    {
        return -1;
    }
    return 0;
}

void
paging_proc_teardown (uint32_t pid)
{
    uint32_t *target_pd = page_directories[pid];

    for (uint32_t pdi = 1; pdi < 768; pdi++)
    {
        uint32_t pde = target_pd[pdi];
        if (!(pde & PAGE_PRESENT))
        {
            continue;
        }

        uint32_t pt_phys = pde & ~0xFFFu;

        /* expose the dead task's PT in the current pgdir to walk it */
        map_page ((void *)pt_phys, (void *)FORK_SCRATCH_PT_VA,
                  PAGE_PRESENT | PAGE_RW);
        uint32_t *pt = (uint32_t *)FORK_SCRATCH_PT_VA;
        for (uint32_t pti = 0; pti < PT_ENTRIES; pti++)
        {
            uint32_t pte = pt[pti];
            if (!(pte & PAGE_PRESENT))
            {
                continue;
            }
            phys_free_frame ((void *)(pte & ~0xFFFu));
        }
        unmap_page ((void *)FORK_SCRATCH_PT_VA);

        phys_free_frame ((void *)pt_phys);
        target_pd[pdi] = PAGE_RW;
    }
}

void
paging_test (void)
{
    pr_info ("#### paging check ####\n");

    /* verify CR0: PE and PG bits */
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
                kpanic ("CR0: PE bit not set");
            }
            if (!(cr0 & 0x80000000))
            {
                kpanic ("CR0: PG bit not set");
            }
        }
    }

    /* verify CR3 points to page directory */
    {
        uint32_t cr3 = read_cr3 ();
        if (cr3 == (uint32_t)page_directories[0])
        {
            pr_info ("CR3: points to page directory (0x%x)\n", cr3);
        }
        else
        {
            kpanic ("CR3: does not point to page directory");
        }
    }

    // identity mapping read/write at 0x100000
    {
        volatile uint32_t *addr = (volatile uint32_t *)0x100000;
        uint32_t old = *addr;
        *addr = 0xDEADBEEF;
        if (*addr == 0xDEADBEEF)
        {
            pr_info ("identity map: read/write at 0x100000\n");
        }
        else
        {
            kpanic ("identity map: read/write failed at 0x100000");
        }
        *addr = old;
    }

    // identity map: get_physaddr must return same address
    {
        void *phys = get_physaddr ((void *)0x100000);
        if (phys == (void *)0x100000)
        {
            pr_info ("identity map: virt 0x100000 resolves to phys 0x100000\n");
        }
        else
        {
            kpanic ("identity map: virt 0x100000 wrong phys resolution");
        }
    }

    // map_page + get_physaddr + read/write + unmap round-trip
    {
        void *vaddr = (void *)0x00800000;
        void *paddr = phys_alloc_frame ();
        if (!paddr)
        {
            kpanic ("map round-trip: could not allocate frame");
        }
        else
        {
            map_page (paddr, vaddr, PAGE_PRESENT | PAGE_RW);
            void *resolved = get_physaddr (vaddr);

            if (resolved == paddr)
            {
                pr_info ("map round-trip: 0x%x -> 0x%x\n", (uint32_t)vaddr,
                         (uint32_t)paddr);
            }
            else
            {
                kpanic ("map round-trip: phys address mismatch");
            }

            volatile uint32_t *ptr = (volatile uint32_t *)vaddr;
            *ptr = 0xCAFEBABE;
            if (*ptr == 0xCAFEBABE)
            {
                pr_info ("map round-trip: read/write through mapping\n");
            }
            else
            {
                kpanic ("map round-trip: read/write failed");
            }

            unmap_page (vaddr);
            void *after = get_physaddr (vaddr);
            if (after == (void *)0)
            {
                pr_info ("unmap: mapping removed at 0x%x\n", (uint32_t)vaddr);
            }
            else
            {
                kpanic ("unmap: mapping still present");
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
            pr_info ("map: unaligned virtual address rejected\n");
        }
        else
        {
            kpanic ("map: unaligned virtual address was accepted");
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
            kpanic ("alloc_page: wrong vaddr returned");
        }

        uint32_t free_during = phys_free_count ();
        if (free_during == free_before - 1)
        {
            pr_info ("alloc_page: consumed one frame (%d -> %d)\n", free_before,
                     free_during);
        }
        else
        {
            kpanic ("alloc_page: free count inconsistent");
        }

        volatile uint32_t *ptr = (volatile uint32_t *)vaddr;
        *ptr = 0x12345678;
        if (*ptr == 0x12345678)
        {
            pr_info ("alloc_page: read/write at 0x%x\n", (uint32_t)vaddr);
        }
        else
        {
            kpanic ("alloc_page: read/write failed");
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
            kpanic ("free_page: frame not returned to PMM");
        }

        void *phys_after = get_physaddr (vaddr);
        if (phys_after == (void *)0)
        {
            pr_info ("free_page: mapping removed at 0x%x\n", (uint32_t)vaddr);
        }
        else
        {
            kpanic ("free_page: mapping still present");
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
            pr_info ("multi-alloc: 3 pages allocated\n");
        }
        else
        {
            kpanic ("multi-alloc: allocation failed");
        }

        void *p1 = get_physaddr (v1);
        void *p2 = get_physaddr (v2);
        void *p3 = get_physaddr (v3);

        if (p1 != p2 && p2 != p3 && p1 != p3)
        {
            pr_info ("multi-alloc: unique physical frames\n");
        }
        else
        {
            kpanic ("multi-alloc: duplicate physical frames");
        }

        *(volatile uint32_t *)v1 = 0xAAAAAAAA;
        *(volatile uint32_t *)v2 = 0xBBBBBBBB;
        *(volatile uint32_t *)v3 = 0xCCCCCCCC;

        if (*(volatile uint32_t *)v1 == 0xAAAAAAAA
            && *(volatile uint32_t *)v2 == 0xBBBBBBBB
            && *(volatile uint32_t *)v3 == 0xCCCCCCCC)
        {
            pr_info ("multi-alloc: no cross-talk between pages\n");
        }
        else
        {
            kpanic ("multi-alloc: cross-talk detected between pages");
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
            kpanic ("free_page: unmapped address changed free count");
        }
    }

    pr_info ("paging check passed\n");
}
