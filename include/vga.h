#ifndef VGA_H
#define VGA_H

#include <stddef.h>
#include <stdint.h>

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

uint8_t vga_entry_color (enum vga_color fg, enum vga_color bg);

void terminal_initialize (void);
void terminal_setcolor (uint8_t color);
uint8_t terminal_getcolor (void);
void terminal_putentryat (char c, uint8_t color, size_t x, size_t y);
void terminal_putchar (char c);
void terminal_write (const char *data, size_t size);
void terminal_writestring (const char *data);

/* Scrollback: delta < 0 = up, delta > 0 = down.
 * Use VGA_USABLE_ROWS as amplitude for a full page. */
#define VGA_USABLE_ROWS 24
void terminal_scroll_lines (int delta);
void terminal_scroll_reset (void);

#endif
