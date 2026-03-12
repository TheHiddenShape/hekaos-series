#include "gdt.h"
#include "idt.h"
#include "interrupts.h"
#include "io.h"
#include "klib.h"
#include "kmalloc.h"
#include "paging.h"
#include "phys_page_frame.h"
#include "pic.h"
#include "printk.h"
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

enum vga_color
{
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

static inline uint8_t
vga_entry_color (enum vga_color fg, enum vga_color bg)
{
    return fg | bg << 4;
}

static inline uint16_t
vga_entry (uint8_t uc, uint8_t color)
{
    return (uint16_t)uc | (uint16_t)color << 8;
}

static const size_t VGA_X = 80;
static const size_t VGA_Y = 25;
static const size_t USABLE_ROWS = 24; // last row reserved for status bar

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint8_t statusbar_color;
volatile uint16_t *terminal_buffer;

// statusbar_putstr – write a string directly into the status bar row
static void
statusbar_putstr (const char *s, size_t col)
{
    while (*s && col < VGA_X)
    {
        terminal_buffer[(VGA_Y - 1) * VGA_X + col]
            = vga_entry (*s, statusbar_color);
        s++;
        col++;
    }
}

// statusbar_putdec – write a decimal number into the status bar, return next
// col
static size_t
statusbar_putdec (uint32_t val, size_t col)
{
    char buf[12];
    int i = 0;

    if (val == 0)
    {
        buf[i++] = '0';
    }
    else
    {
        while (val > 0)
        {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    // reverse and write
    while (i > 0 && col < VGA_X)
    {
        terminal_buffer[(VGA_Y - 1) * VGA_X + col]
            = vga_entry (buf[--i], statusbar_color);
        col++;
    }
    return col;
}

// statusbar_draw – fill the bottom row with status info
static void
statusbar_draw (void)
{
    // fill entire row with spaces
    for (size_t x = 0; x < VGA_X; x++)
    {
        terminal_buffer[(VGA_Y - 1) * VGA_X + x]
            = vga_entry (' ', statusbar_color);
    }

    // left side: mode info (1 space padding)
    statusbar_putstr (" VGA text mode 80x25", 0);

    // right side: build "col:X row:Y " into a temp buffer, then write
    // right-aligned
    char right[32];
    int ri = 0;
    // "col:"
    right[ri++] = 'c';
    right[ri++] = 'o';
    right[ri++] = 'l';
    right[ri++] = ':';
    // column number
    {
        char tmp[6];
        int ti = 0;
        uint32_t v = (uint32_t)terminal_column;
        if (v == 0)
        {
            tmp[ti++] = '0';
        }
        else
        {
            while (v > 0)
            {
                tmp[ti++] = '0' + (v % 10);
                v /= 10;
            }
        }
        while (ti > 0)
        {
            right[ri++] = tmp[--ti];
        }
    }
    right[ri++] = ' ';
    // "row:"
    right[ri++] = 'r';
    right[ri++] = 'o';
    right[ri++] = 'w';
    right[ri++] = ':';
    // row number
    {
        char tmp[6];
        int ti = 0;
        uint32_t v = (uint32_t)terminal_row;
        if (v == 0)
        {
            tmp[ti++] = '0';
        }
        else
        {
            while (v > 0)
            {
                tmp[ti++] = '0' + (v % 10);
                v /= 10;
            }
        }
        while (ti > 0)
        {
            right[ri++] = tmp[--ti];
        }
    }
    right[ri++] = ' ';

    // write right-aligned (1 space padding from edge)
    size_t start = VGA_X - (size_t)ri;
    for (int j = 0; j < ri; j++)
    {
        terminal_buffer[(VGA_Y - 1) * VGA_X + start + (size_t)j]
            = vga_entry (right[j], statusbar_color);
    }
}

void
terminal_initialize (void)
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = (uint16_t *)0xB8000;

    // pick status bar theme based on CPU cycle counter parity
    if (rdtsc () & 1)
    {
        statusbar_color = vga_entry_color (VGA_COLOR_WHITE, VGA_COLOR_RED);
    }
    else
    {
        statusbar_color = vga_entry_color (VGA_COLOR_RED, VGA_COLOR_WHITE);
    }

    // clear usable area
    for (size_t y = 0; y < USABLE_ROWS; y++)
    {
        for (size_t x = 0; x < VGA_X; x++)
        {
            terminal_buffer[y * VGA_X + x] = vga_entry (' ', terminal_color);
        }
    }

    statusbar_draw ();
}

void
terminal_setcolor (uint8_t color)
{
    terminal_color = color;
}

void
terminal_update_cursor (void)
{
    uint16_t pos = terminal_row * VGA_X + terminal_column;
    outb (0x3D4, 0x0F);
    outb (0x3D5, (uint8_t)(pos & 0xFF));
    outb (0x3D4, 0x0E);
    outb (0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void
terminal_putentryat (char c, uint8_t color, size_t x, size_t y)
{
    const size_t index = y * VGA_X + x;
    terminal_buffer[index] = vga_entry (c, color);
}

void
terminal_scroll (void)
{
    for (size_t y = 0; y < USABLE_ROWS - 1; y++)
    {
        for (size_t x = 0; x < VGA_X; x++)
        {
            terminal_buffer[y * VGA_X + x]
                = terminal_buffer[(y + 1) * VGA_X + x];
        }
    }
    for (size_t x = 0; x < VGA_X; x++)
    {
        terminal_buffer[(USABLE_ROWS - 1) * VGA_X + x]
            = vga_entry (' ', terminal_color);
    }
}

void
terminal_putchar (char c)
{
    if (c == '\n')
    {
        terminal_column = 0;
        if (++terminal_row == USABLE_ROWS)
        {
            terminal_scroll ();
            terminal_row = USABLE_ROWS - 1;
        }
        terminal_update_cursor ();
        statusbar_draw ();
        return;
    }
    if (c == '\r')
    {
        terminal_column = 0;
        terminal_update_cursor ();
        statusbar_draw ();
        return;
    }
    if (c == '\b')
    {
        if (terminal_column > 0)
        {
            terminal_column--;
            terminal_putentryat (' ', terminal_color, terminal_column,
                                 terminal_row);
            terminal_update_cursor ();
            statusbar_draw ();
        }
        return;
    }
    terminal_putentryat (c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column == VGA_X)
    {
        terminal_column = 0;
        if (++terminal_row == USABLE_ROWS)
        {
            terminal_scroll ();
            terminal_row = USABLE_ROWS - 1;
        }
    }
    terminal_update_cursor ();
    statusbar_draw ();
}

void
terminal_write (const char *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        terminal_putchar (data[i]);
    }
}

void
terminal_writestring (const char *data)
{
    terminal_write (data, strlen (data));
}

#define CMD_BUFFER_SIZE 256
char cmd_buffer[CMD_BUFFER_SIZE];
size_t cmd_index = 0;

static const char *SHELL_PROMPT = "hekashell> ";

void
shell_print_prompt (void)
{
    uint8_t name_color = vga_entry_color (VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    uint8_t symbol_color
        = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    uint8_t saved_color = terminal_color;
    terminal_setcolor (name_color);
    terminal_writestring ("hekashell");
    terminal_setcolor (symbol_color);
    terminal_writestring ("> ");
    terminal_setcolor (saved_color);
}

void
shell_reboot (void)
{
    terminal_writestring ("Rebooting...\n");
    // use keyboard controller to trigger system reset
    outb (0x64, 0xFE);
    // if that fails, halt
    disable_interrupts_and_halt ();
}

void
shell_halt (void)
{
    terminal_writestring ("System halted.\n");
    disable_interrupts_and_halt ();
}

void
shell_shutdown (void)
{
    terminal_writestring ("Shutting down...\n");
    // ACPI shutdown: write SLP_EN (bit 13) to PM1a control block
    // QEMU PIIX4: port 0x604, value 0x2000
    outw (0x604, 0x2000);
    // Bochs / older QEMU fallback
    outw (0xB004, 0x2000);
    // VirtualBox fallback
    outw (0x4004, 0x3400);
    // if all fail, halt
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
    terminal_writestring ("=== memory dump ===\n");

    kmalloc_stats_t ks;
    kmalloc_query (&ks);
    terminal_writestring ("kmalloc: heap=");
    terminal_write_hex (KHEAP_VIRT_BASE);
    terminal_writestring ("-");
    terminal_write_hex (ks.heap_end);
    terminal_writestring (" | ");
    terminal_write_uint (ks.total_blocks);
    terminal_writestring (" blocks (");
    terminal_write_uint (ks.used_blocks);
    terminal_writestring (" used, ");
    terminal_write_uint (ks.free_blocks);
    terminal_writestring (" free) | ");
    terminal_write_uint (ks.used_bytes);
    terminal_writestring ("B used / ");
    terminal_write_uint (ks.free_bytes);
    terminal_writestring ("B free\n");

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
    terminal_writestring ("Available commands:\n");
    terminal_writestring ("  help     - Show this help message\n");
    terminal_writestring ("  dmesg    - Display kernel ring buffer\n");
    terminal_writestring ("  memdump  - Display memory usage summary\n");
    terminal_writestring ("  reboot   - Reboot the system\n");
    terminal_writestring ("  shutdown - Power off the system (ACPI)\n");
    terminal_writestring ("  halt     - Halt the CPU\n");
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
    else if (cmd[0] != '\0')
    {
        terminal_writestring ("Unknown command: ");
        terminal_writestring (cmd);
        terminal_writestring ("\nType 'help' for available commands.\n");
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

    pr_info ("#### Kernel Stack ####\n");
    pr_info ("Stack Top:        %p\n", (void *)stack_top_addr);
    pr_info ("Stack Bottom:     %p\n", (void *)stack_bottom_addr);
    pr_info ("Stack Size:       %d bytes (%d KB)\n", stack_size,
             stack_size / 1024);
    pr_info ("Current ESP:      %p\n", (void *)esp);
    pr_info ("Used:             %d bytes\n", stack_used);
    pr_info ("Free:             %d bytes\n", stack_free);
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
    heap_init ();
    kmalloc_test ();
    vmalloc_init ();
    vmalloc_test ();

    print_banner ();

    kprint_stack_info ();
    klog_gdt_info ();

    pr_info ("HekaOS v0.1.0 initialized\n");

    terminal_writestring ("Welcome to HekaOS, type help to get started !\n\n");
    shell_print_prompt ();

    enable_interrupts ();

    while (1)
    {
        halt_cpu ();
    }
}
