#ifndef TRAP_FRAME_H
#define TRAP_FRAME_H

#include <stdint.h>

/*
 * The trap frame is a data structure heavily inspired by implementations found
 * in well-known kernels. Its purpose is to save the CPU-correlated state at a
 * given instant T following an exception, interrupt, context switch, or kernel
 * panic. The intent is to allow troubleshooting or restoration of the
 * previous execution state.
 */

struct trap_frame
{
    /* pushed manually by the stubs */
    uint32_t ds;

    /* pushad: EDI is first on the stack */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_saved;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t int_no;
    uint32_t err_code;

    /* pushed automatically by the CPU */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    /* only present on privilege level change ring3 -> ring0 */
    uint32_t user_esp;
    uint32_t user_ss;
};

void trap_frame_display (struct trap_frame *frame);
void trap_frame_stack_trace (struct trap_frame *frame);
void trigger_trap_test (void);

#endif
