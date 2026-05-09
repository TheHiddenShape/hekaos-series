#include "gdt.h"
#include "idt.h"
#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "klib.h"
#include "kmalloc.h"
#include "kmem_dyn_alloc.h"
#include "paging.h"
#include "phys_page_frame.h"
#include "pic.h"
#include "printk.h"
#include "trap_frame.h"
#include "vga.h"
#include "vmalloc.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__linux__)
#error                                                                         \
    "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

#if !defined(__i386__)
#error "Needs to be compiled with a ix86-elf compiler"
#endif

#define CMD_BUFFER_SIZE 256
char cmd_buffer[CMD_BUFFER_SIZE];
size_t cmd_index = 0;

void
shell_print_prompt (void)
{
    uint8_t name_color = vga_entry_color (VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    uint8_t symbol_color
        = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    uint8_t saved_color = terminal_getcolor ();
    terminal_setcolor (name_color);
    terminal_writestring ("hekashell");
    terminal_setcolor (symbol_color);
    terminal_writestring ("> ");
    terminal_setcolor (saved_color);
}

void
shell_reboot (void)
{
    terminal_writestring ("rebooting...\n");
    /* use keyboard controller to trigger system reset */
    outb (0x64, 0xFE);
    /* if that fails, halt */
    disable_interrupts_and_halt ();
}

void
shell_halt (void)
{
    terminal_writestring ("system halted.\n");
    disable_interrupts_and_halt ();
}

void
shell_shutdown (void)
{
    terminal_writestring ("shutting down...\n");
    /* ACPI shutdown: write SLP_EN (bit 13) to PM1a control block */
    /* QEMU PIIX4: port 0x604, value 0x2000 */
    outw (0x604, 0x2000);
    /* Bochs / older QEMU fallback */
    outw (0xB004, 0x2000);
    /* VirtualBox fallback */
    outw (0x4004, 0x3400);
    /* if all fail, halt */
    terminal_writestring ("ACPI shutdown failed, halting.\n");
    disable_interrupts_and_halt ();
}

static void
terminal_write_uint (uint32_t value)
{
    char buf[11];
    char *p = buf + sizeof (buf) - 1;
    *p = '\0';
    if (!value)
    {
        *--p = '0';
    }
    else
    {
        while (value)
        {
            *--p = '0' + (value % 10);
            value /= 10;
        }
    }
    terminal_writestring (p);
}

static void
terminal_write_hex (uint32_t value)
{
    terminal_writestring ("0x");
    if (!value)
    {
        terminal_putchar ('0');
        return;
    }
    char buf[9];
    char *p = buf + sizeof (buf) - 1;
    *p = '\0';
    while (value)
    {
        *--p = "0123456789abcdef"[value % 16];
        value /= 16;
    }
    terminal_writestring (p);
}

static void
shell_memdump (void)
{
    terminal_writestring ("#### memory dump ####\n");

    kmalloc_stats_t ks;
    kmalloc_query (&ks);
    terminal_writestring ("kmalloc (chunk pool): zone=");
    terminal_write_hex (KCPOOL_VIRT_BASE);
    terminal_writestring (" | ");
    terminal_write_uint (ks.pool_pages);
    terminal_writestring (" pages | ");
    terminal_write_uint (ks.free_objects);
    terminal_writestring (" free objects\n");

    vmalloc_stats_t vs;
    vmalloc_query (&vs);
    terminal_writestring ("vmalloc: zone=");
    terminal_write_hex (VMALLOC_BASE);
    terminal_writestring ("-");
    terminal_write_hex (VMALLOC_MAX);
    terminal_writestring (" | ");
    terminal_write_uint (vs.used_pages);
    terminal_writestring ("/");
    terminal_write_uint (vs.total_pages);
    terminal_writestring (" pages used\n");
}

void
shell_help (void)
{
    terminal_writestring ("available commands:\n");
    terminal_writestring ("  help     - show this help message\n");
    terminal_writestring ("  dmesg    - display kernel ring buffer\n");
    terminal_writestring ("  memdump  - display memory usage summary\n");
    terminal_writestring ("  reboot   - reboot the system\n");
    terminal_writestring ("  shutdown - power off the system (ACPI)\n");
    terminal_writestring ("  halt     - halt the CPU\n");
    terminal_writestring (
        "  traptest - trigger INT 0x42 and dump the trap frame\n");
    terminal_writestring ("  keymap   - qwerty | azerty\n");
}

void
shell_execute (const char *cmd)
{
    if (strcmp (cmd, "reboot") == 0)
    {
        shell_reboot ();
    }
    else if (strcmp (cmd, "shutdown") == 0)
    {
        shell_shutdown ();
    }
    else if (strcmp (cmd, "halt") == 0)
    {
        shell_halt ();
    }
    else if (strcmp (cmd, "help") == 0)
    {
        shell_help ();
    }
    else if (strcmp (cmd, "dmesg") == 0)
    {
        dmesg ();
    }
    else if (strcmp (cmd, "memdump") == 0)
    {
        shell_memdump ();
    }
    else if (strcmp (cmd, "traptest") == 0)
    {
        terminal_writestring ("triggering INT 0x42 (trap frame test)...\n");
        trigger_trap_test ();
        terminal_writestring ("returned from trap, trap frame OK\n");
    }
    else if (strcmp (cmd, "keymap azerty") == 0)
    {
        set_keymap (KEYMAP_AZERTY);
        terminal_writestring ("keymap: azerty\n");
    }
    else if (strcmp (cmd, "keymap qwerty") == 0)
    {
        set_keymap (KEYMAP_QWERTY);
        terminal_writestring ("keymap: qwerty\n");
    }
    else if (cmd[0] != '\0')
    {
        terminal_writestring ("unknown command: ");
        terminal_writestring (cmd);
        terminal_writestring ("\ntype 'help' for available commands.\n");
    }
    shell_print_prompt ();
}

void
shell_add_char (char c)
{
    if (c == '\n')
    {
        terminal_putchar ('\n');
        cmd_buffer[cmd_index] = '\0';
        shell_execute (cmd_buffer);
        cmd_index = 0;
    }
    else if (c == '\b')
    {
        if (cmd_index > 0)
        {
            cmd_index--;
            terminal_putchar ('\b');
        }
    }
    else if (cmd_index < CMD_BUFFER_SIZE - 1)
    {
        cmd_buffer[cmd_index++] = c;
        terminal_putchar (c);
    }
}

extern char stack_bottom;
extern char stack_top;

void
kprint_stack_info (void)
{
    uint32_t esp = get_esp ();

    uint32_t stack_top_addr = (uint32_t)&stack_top;
    uint32_t stack_bottom_addr = (uint32_t)&stack_bottom;
    uint32_t stack_size = stack_top_addr - stack_bottom_addr;
    uint32_t stack_used = stack_top_addr - esp;
    uint32_t stack_free = esp - stack_bottom_addr;

    pr_info ("#### kernel stack ####\n");
    pr_info ("stack top:        %p\n", (void *)stack_top_addr);
    pr_info ("stack bottom:     %p\n", (void *)stack_bottom_addr);
    pr_info ("stack size:       %d bytes (%d KB)\n", stack_size,
             stack_size / 1024);
    pr_info ("current ESP:      %p\n", (void *)esp);
    pr_info ("used:             %d bytes\n", stack_used);
    pr_info ("free:             %d bytes\n", stack_free);
    printk ("\n");
}

void
print_banner (void)
{
    uint8_t border_color = vga_entry_color (VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t title_color = vga_entry_color (VGA_COLOR_RED, VGA_COLOR_BLACK);
    uint8_t text_color
        = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    terminal_setcolor (border_color);
    terminal_writestring ("\n");
    terminal_writestring (
        "************************************************************\n");
    terminal_writestring (
        "*                                                          *\n");
    terminal_writestring ("*  ");
    terminal_setcolor (title_color);
    terminal_writestring ("##  ##  ######  ##  ##   ###    ####    ####");
    terminal_setcolor (border_color);
    terminal_writestring ("            *\n*  ");
    terminal_setcolor (title_color);
    terminal_writestring ("##  ##  ##      ## ##   ## ##  ##  ##  ##   ");
    terminal_setcolor (border_color);
    terminal_writestring ("            *\n*  ");
    terminal_setcolor (title_color);
    terminal_writestring ("######  ####    ####   ##  ##  ##  ##   ### ");
    terminal_setcolor (border_color);
    terminal_writestring ("            *\n*  ");
    terminal_setcolor (title_color);
    terminal_writestring ("##  ##  ##      ## ##  ######  ##  ##     ##");
    terminal_setcolor (border_color);
    terminal_writestring ("            *\n*  ");
    terminal_setcolor (title_color);
    terminal_writestring ("##  ##  ######  ##  ## ##  ##   ####   #### ");
    terminal_setcolor (border_color);
    terminal_writestring ("            *\n");
    terminal_writestring (
        "*                                                          *\n");
    terminal_writestring (
        "************************************************************\n");
    terminal_writestring ("\n");
    terminal_setcolor (text_color);
}

void
kernel_main (void)
{
    terminal_initialize ();

    gdt_init ();
    pic_remap ();
    idt_init ();
    paging_init ();
    phys_mem_init ();
    phys_mem_test ();
    paging_test ();
    kmem_dyn_alloc_init ();
    kmem_dyn_alloc_test ();
    kmalloc_init ();
    kmalloc_test ();
    vmalloc_init ();
    vmalloc_test ();

    print_banner ();

    kprint_stack_info ();
    klog_gdt_info ();

    pr_info ("hekaOS v0.3.0 initialized\n");

    terminal_writestring ("Welcome to hekaOS, type help to get started !\n\n");
    shell_print_prompt ();

    enable_interrupts ();

    while (1)
    {
        halt_cpu ();
    }
}
