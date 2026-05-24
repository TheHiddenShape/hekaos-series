#include "tss.h"
#include "klib.h"

/* CPU-wide singleton TSS. Pointed to by GDT entry 7 (selector 0x38) and
 * loaded into TR by tss_flush. Only ss0/esp0/iomap_base carry meaningful
 * values; the rest is dead weight imposed by the hardware layout. */
struct tss_entry tss;

void
tss_init (void)
{
    memset (&tss, 0, sizeof (tss));
    tss.ss0 = 0x10;                /* kernel data segment */
    tss.esp0 = 0;                  /* set on the first user-bound switch */
    tss.iomap_base = sizeof (tss); /* no I/O permission bitmap */
}

void
tss_set_esp0 (uint32_t esp0)
{
    tss.esp0 = esp0;
}
