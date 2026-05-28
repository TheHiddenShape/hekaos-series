#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "kpanic.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "printk.h"
#include "signal.h"
#include "syscall.h"
#include "task.h"
#include "trap_frame.h"
#include "vga.h"
#include <stdbool.h>
#include <stdint.h>

extern void shell_add_char (char c);

/* scancode to ASCII table (US QWERTY, lowercase) */
static const char keymap_qwerty[128]
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

/* scancode to ASCII table (FR AZERTY, lowercase) */
static const char keymap_azerty[128]
    = { 0,   27,   '&',  0,   '"', '\'', '(', '-', 0,   '_', 0,   0,   ')',
        '=', '\b', '\t', 'a', 'z', 'e',  'r', 't', 'y', 'u', 'i', 'o', 'p',
        '^', '$',  '\n', 0,   'q', 's',  'd', 'f', 'g', 'h', 'j', 'k', 'l',
        'm', 0,    0,    0,   '*', 'w',  'x', 'c', 'v', 'b', 'n', ',', ';',
        ':', '!',  0,    '*', 0,   ' ',  0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,
        0,   0,    0,    0,   0,   0,    0,   0,   0,   0 };

static const char *active_keymap = keymap_qwerty;

void
set_keymap (keymap_t layout)
{
    switch (layout)
    {
        case KEYMAP_AZERTY:
            active_keymap = keymap_azerty;
            break;
        case KEYMAP_QWERTY:
        default:
            active_keymap = keymap_qwerty;
            break;
    }
}

static void
gpf_handler (struct trap_frame *frame)
{
    trap_frame_display (frame);
    kpanic ("general protection fault");
}

static void
page_fault_handler (struct trap_frame *frame)
{
    pr_err ("page fault: faulting address=0x%x err=0x%x\n", read_cr2 (),
            frame->err_code);
    trap_frame_display (frame);
    kpanic ("page fault");
}

static void
unhandled_exception (struct trap_frame *frame)
{
    trap_frame_display (frame);
    kpanic ("unhandled exception");
}

__attribute__ ((noreturn)) void
cpu_park_dying (void)
{
    while (1)
    {
        enable_interrupts ();
        halt_cpu ();
    }
}

static void
kill_user_on_fault (struct trap_frame *frame)
{
    kernel_signal_send (current_task, signal_from_exception (frame->int_no));
    cpu_park_dying ();
}

void
isr_handler (struct trap_frame *frame)
{
    switch (frame->int_no)
    {
        case 0:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("division by zero");
            break;
        case 1: /* todo: hook into ptrace/kgdb debug trap in userspace */
            trap_frame_display (frame);
            kpanic ("debugger exception");
            break;
        case 2: /* NMI, always kernel level: watchdog, hardware error */
            trap_frame_display (frame);
            kpanic ("non-maskable interrupt");
            break;
        case 3: /* todo: hook into ptrace/kgdb breakpoint in userspace */
            trap_frame_display (frame);
            kpanic ("breakpoint");
            break;
        case 4:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("overflow");
            break;
        case 5:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("bound range exceeded");
            break;
        case 6:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("invalid opcode");
            break;
        case 7: /* todo: implement lazy FPU context switch in userspace */
            trap_frame_display (frame);
            kpanic ("coprocessor not available");
            break;
        case 8: /* Double Fault, always panic, unrecoverable */
            trap_frame_display (frame);
            kpanic ("double fault");
            break;
        case 9: /* todo: check CPL, send SIGFPE to process in userspace */
            trap_frame_display (frame);
            kpanic ("coprocessor segment overrun");
            break;
        case 10:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("invalid task state segment");
            break;
        case 11:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("segment not present");
            break;
        case 12:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("stack fault");
            break;
        case 13:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            gpf_handler (frame);
            break;
        case 14:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            page_fault_handler (frame);
            break;
        case 15:
            trap_frame_display (frame);
            kpanic ("reserved exception (0x0f)");
            break;
        case 16:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("x87 math fault");
            break;
        case 17:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("alignment check");
            break;
        case 18: /* Machine Check, always panic, hardware error */
            trap_frame_display (frame);
            kpanic ("machine check");
            break;
        case 19:
            if ((frame->cs & 3) == 3)
            {
                kill_user_on_fault (frame);
            }
            trap_frame_display (frame);
            kpanic ("simd floating-point exception");
            break;
        case 128:
            syscall_dispatch (frame);
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
        case 32: /* IRQ 0 = PIT timer */
        {
            timer_tick ();
            break;
        }
        case 33: /* IRQ 1 = keyboard */
        {
            static bool extended = false;
            uint8_t scancode = inb (0x60);

            if (scancode == 0xE0)
            {
                extended = true;
                break;
            }
            if (extended)
            {
                extended = false;
                if (!(scancode & 0x80))
                {
                    switch (scancode)
                    {
                        case 0x48: /* arrow up - scroll one line up */
                            terminal_scroll_lines (-1);
                            break;
                        case 0x50: /* arrow down - scroll one line down */
                            terminal_scroll_lines (1);
                            break;
                        case 0x49: /* page up - scroll one page up */
                            terminal_scroll_lines (-VGA_USABLE_ROWS);
                            break;
                        case 0x51: /* page down - scroll one page down */
                            terminal_scroll_lines (VGA_USABLE_ROWS);
                            break;
                        default:
                            break;
                    }
                }
                break;
            }
            /* standard scancode */
            if (!(scancode & 0x80))
            {
                terminal_scroll_reset ();
                char c = active_keymap[scancode];
                if (c)
                {
                    shell_add_char (c);
                }
            }
            break;
        }
        default:
            pr_err ("Unhandled IRQ: vector %d\n", frame->int_no);
            break;
    }

    pic_send_eoi (frame->int_no - 32);
}
