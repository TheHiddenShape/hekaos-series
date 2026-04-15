#include "interrupts.h"
#include "io.h"
#include "kpanic.h"
#include "paging.h"
#include "pic.h"
#include "printk.h"
#include "trap_frame.h"
#include <stdbool.h>
#include <stdint.h>

extern void shell_add_char (char c);

/* scancode to ASCII table (US QWERTY, lowercase) */
static const char scancode_to_ascii[128]
    = { 0,   27,   '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
        '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
        '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
        ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
        '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,    0,   0,   0,   0,   0 };

static void
gpf_handler (struct trap_frame *frame)
{
    trap_frame_display (frame);
    kpanic ("General Protection Fault");
}

static void
page_fault_handler (struct trap_frame *frame)
{
    pr_err ("Page Fault: faulting address=0x%x err=0x%x\n", read_cr2 (),
            frame->err_code);
    trap_frame_display (frame);
    kpanic ("Page Fault");
}

static void
unhandled_exception (struct trap_frame *frame)
{
    trap_frame_display (frame);
    kpanic ("Unhandled Exception");
}

void
isr_handler (struct trap_frame *frame)
{
    switch (frame->int_no)
    {
        case 13:
            gpf_handler (frame);
            break;
        case 14:
            page_fault_handler (frame);
            break;
        case 66:
        {
            bool pass = true;
            trap_frame_display (frame);
            trap_frame_stack_trace (frame);
            if (frame->int_no != 66)
            {
                pr_err ("FAIL: int_no=%d\n", frame->int_no);
                pass = false;
            }
            if (frame->err_code != 0)
            {
                pr_err ("FAIL: err_code=0x%x\n", frame->err_code);
                pass = false;
            }
            if (frame->eax != 0xAAAAAAAA)
            {
                pr_err ("FAIL: eax=0x%x\n", frame->eax);
                pass = false;
            }
            if (frame->ebx != 0xBBBBBBBB)
            {
                pr_err ("FAIL: ebx=0x%x\n", frame->ebx);
                pass = false;
            }
            if (frame->ecx != 0xCCCCCCCC)
            {
                pr_err ("FAIL: ecx=0x%x\n", frame->ecx);
                pass = false;
            }
            if (frame->edx != 0xDDDDDDDD)
            {
                pr_err ("FAIL: edx=0x%x\n", frame->edx);
                pass = false;
            }
            if (frame->esi != 0x11111111)
            {
                pr_err ("FAIL: esi=0x%x\n", frame->esi);
                pass = false;
            }
            if (frame->edi != 0x22222222)
            {
                pr_err ("FAIL: edi=0x%x\n", frame->edi);
                pass = false;
            }
            if (frame->cs != 0x08)
            {
                pr_err ("FAIL: cs=0x%x\n", frame->cs);
                pass = false;
            }
            if (pass)
            {
                pr_debug ("trap_frame canary test: [OK]\n");
            }
            break;
        }
        default:
            unhandled_exception (frame);
            break;
    }
}

void
irq_handler (struct trap_frame *frame)
{
    switch (frame->int_no)
    {
        case 33: /* IRQ 1 = keyboard */
        {
            uint8_t scancode = inb (0x60);

            if (!(scancode & 0x80))
            {
                char c = scancode_to_ascii[scancode];
                if (c)
                    shell_add_char (c);
            }
            break;
        }
        default:
            pr_err ("Unhandled IRQ: vector %d\n", frame->int_no);
            break;
    }

    pic_send_eoi (frame->int_no - 32);
}
