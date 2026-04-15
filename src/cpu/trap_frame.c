#include "trap_frame.h"
#include "printk.h"
#include <stddef.h>

/*
 * Compile-time guard: ensures no accidental padding is introduced if the
 * struct is ever modified (e.g. a uint8_t/uint16_t field added). Since all
 * fields are uint32_t the offsets are correct by construction, but this
 * makes any future regression an immediate build error.
 */
_Static_assert (offsetof (struct trap_frame, ds) == 0,
                "trap_frame: ds offset wrong");
_Static_assert (offsetof (struct trap_frame, edi) == 4,
                "trap_frame: edi offset wrong");
_Static_assert (offsetof (struct trap_frame, esi) == 8,
                "trap_frame: esi offset wrong");
_Static_assert (offsetof (struct trap_frame, ebp) == 12,
                "trap_frame: ebp offset wrong");
_Static_assert (offsetof (struct trap_frame, esp_saved) == 16,
                "trap_frame: esp_saved offset wrong");
_Static_assert (offsetof (struct trap_frame, ebx) == 20,
                "trap_frame: ebx offset wrong");
_Static_assert (offsetof (struct trap_frame, edx) == 24,
                "trap_frame: edx offset wrong");
_Static_assert (offsetof (struct trap_frame, ecx) == 28,
                "trap_frame: ecx offset wrong");
_Static_assert (offsetof (struct trap_frame, eax) == 32,
                "trap_frame: eax offset wrong");
_Static_assert (offsetof (struct trap_frame, int_no) == 36,
                "trap_frame: int_no offset wrong");
_Static_assert (offsetof (struct trap_frame, err_code) == 40,
                "trap_frame: err_code offset wrong");
_Static_assert (offsetof (struct trap_frame, eip) == 44,
                "trap_frame: eip offset wrong");
_Static_assert (offsetof (struct trap_frame, cs) == 48,
                "trap_frame: cs offset wrong");
_Static_assert (offsetof (struct trap_frame, eflags) == 52,
                "trap_frame: eflags offset wrong");
_Static_assert (offsetof (struct trap_frame, user_esp) == 56,
                "trap_frame: user_esp offset wrong");
_Static_assert (offsetof (struct trap_frame, user_ss) == 60,
                "trap_frame: user_ss offset wrong");
_Static_assert (sizeof (struct trap_frame) == 64,
                "trap_frame: size wrong (padding or mistyped field)");

void
trap_frame_display (struct trap_frame *tframe)
{
    pr_debug ("#### trap_frame dump ####\n");
    pr_debug ("  INT=%d  ERR=0x%x\n", tframe->int_no, tframe->err_code);
    pr_debug ("  EIP=0x%x  CS=0x%x  EFLAGS=0x%x\n", tframe->eip, tframe->cs,
              tframe->eflags);
    pr_debug ("  EAX=0x%x  EBX=0x%x  ECX=0x%x  EDX=0x%x\n", tframe->eax,
              tframe->ebx, tframe->ecx, tframe->edx);
    pr_debug ("  ESI=0x%x  EDI=0x%x  EBP=0x%x  ESP=0x%x\n", tframe->esi,
              tframe->edi, tframe->ebp, tframe->esp_saved);
    pr_debug ("  DS=0x%x\n", tframe->ds);
    printk ("\n");
}
