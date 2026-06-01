#include "vga.h"
#include "io.h"
#include "klib.h"
#include <stddef.h>
#include <stdint.h>

/*
 * VGA text mode driver - hardware scrolling + keyboard scrollback.
 *
 * VGA memory: 0xB8000-0xBFFFF = 32768 bytes
 * Each cell: 2 bytes (char + color attribute)
 * Available rows: 32768 / (80 * 2) = 204 complete rows (~8 pages of 25)
 *
 * Hardware scrolling: instead of memmove on every newline, we advance
 * the CRTC start address (registers 0x0C/0x0D at port 0x3D4/0x3D5).
 * Only the new bottom row is cleared.
 *
 * Status bar: always drawn at the bottom of the DISPLAYED view.
 * In normal mode that is (vga_view_top + USABLE_ROWS).
 * In scrollback mode we save the content of the new bottom row to a
 * small RAM buffer, draw the bar there, and restore when leaving.
 */

#define VGA_X 80
#define VGA_Y 25
#define USABLE_ROWS 24
#define VGA_TOTAL_ROWS 204
#define VGA_MAX_VIEW_TOP (VGA_TOTAL_ROWS - VGA_Y)
#define SCROLLBACK_ROWS (VGA_TOTAL_ROWS - 2 * VGA_Y)
#define SCROLLBAR_BAR_LEN 62

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint8_t statusbar_color;
static size_t vga_view_top;
static size_t scroll_back;

/* one row of RAM used to save/restore the line overwritten by the
 * scrollback status bar */
static uint16_t saved_bar[VGA_X];
static size_t saved_bar_row;
static int saved_bar_valid;

static volatile uint16_t *terminal_buffer;

uint8_t
vga_entry_color (enum vga_color fg, enum vga_color bg)
{
    return (uint8_t)fg | (uint8_t)bg << 4;
}

static inline uint16_t
vga_entry (uint8_t uc, uint8_t color)
{
    return (uint16_t)uc | (uint16_t)color << 8;
}

static void
vga_set_start (size_t row)
{
    uint16_t addr = (uint16_t)(row * VGA_X);
    outb (0x3D4, 0x0C);
    outb (0x3D5, (uint8_t)((addr >> 8) & 0xFF));
    outb (0x3D4, 0x0D);
    outb (0x3D5, (uint8_t)(addr & 0xFF));
}

static void
terminal_update_cursor (void)
{
    if (scroll_back > 0)
    {
        outb (0x3D4, 0x0F);
        outb (0x3D5, 0xFF);
        outb (0x3D4, 0x0E);
        outb (0x3D5, 0xFF);
        return;
    }
    uint16_t pos = (uint16_t)(terminal_row * VGA_X + terminal_column);
    outb (0x3D4, 0x0F);
    outb (0x3D5, (uint8_t)(pos & 0xFF));
    outb (0x3D4, 0x0E);
    outb (0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void
terminal_set_cursor (size_t x, size_t y)
{
    uint16_t pos = (uint16_t)(y * VGA_X + x);
    outb (0x3D4, 0x0F);
    outb (0x3D5, (uint8_t)(pos & 0xFF));
    outb (0x3D4, 0x0E);
    outb (0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void
bar_write_str (size_t base, const char *s, size_t col)
{
    while (*s && col < VGA_X)
    {
        terminal_buffer[base + col] = vga_entry ((uint8_t)*s, statusbar_color);
        s++;
        col++;
    }
}

/* Draw the normal status bar at a given VGA row. */
static void
statusbar_draw_at (size_t row)
{
    size_t base = row * VGA_X;
    for (size_t x = 0; x < VGA_X; x++)
        terminal_buffer[base + x] = vga_entry (' ', statusbar_color);

    bar_write_str (base, " VGA text 80x25", 0);

    char right[32];
    int ri = 0;
    right[ri++] = 'c';
    right[ri++] = 'o';
    right[ri++] = 'l';
    right[ri++] = ':';
    {
        char tmp[6];
        int ti = 0;
        uint32_t v = (uint32_t)terminal_column;
        if (v == 0)
            tmp[ti++] = '0';
        else
            while (v)
            {
                tmp[ti++] = '0' + (v % 10);
                v /= 10;
            }
        while (ti > 0)
            right[ri++] = tmp[--ti];
    }
    right[ri++] = ' ';
    right[ri++] = 'r';
    right[ri++] = 'o';
    right[ri++] = 'w';
    right[ri++] = ':';
    {
        char tmp[6];
        int ti = 0;
        uint32_t v = (uint32_t)(terminal_row - vga_view_top);
        if (v == 0)
            tmp[ti++] = '0';
        else
            while (v)
            {
                tmp[ti++] = '0' + (v % 10);
                v /= 10;
            }
        while (ti > 0)
            right[ri++] = tmp[--ti];
    }
    right[ri++] = ' ';

    size_t start = VGA_X - (size_t)ri;
    for (int j = 0; j < ri; j++)
        terminal_buffer[base + start + (size_t)j]
            = vga_entry ((uint8_t)right[j], statusbar_color);
}

/* Draw a scrollback indicator bar at a given VGA row.
 * Filled blocks (0xDB) grow left-to-right as scroll_back increases. */
static void
scrollbar_draw_at (size_t row)
{
    size_t base = row * VGA_X;

    for (size_t x = 0; x < VGA_X; x++)
        terminal_buffer[base + x] = vga_entry (' ', statusbar_color);

    size_t max_back
        = vga_view_top < SCROLLBACK_ROWS ? vga_view_top : SCROLLBACK_ROWS;

    size_t filled
        = (max_back > 0) ? (scroll_back * SCROLLBAR_BAR_LEN) / max_back : 0;
    if (filled > SCROLLBAR_BAR_LEN)
        filled = SCROLLBAR_BAR_LEN;

    terminal_buffer[base] = vga_entry ('[', statusbar_color);
    for (size_t i = 0; i < SCROLLBAR_BAR_LEN; i++)
        terminal_buffer[base + 1 + i]
            = vga_entry (i < filled ? 0xDB : 0xB0, statusbar_color);
    terminal_buffer[base + 1 + SCROLLBAR_BAR_LEN]
        = vga_entry (']', statusbar_color);

    uint32_t pg_now = (uint32_t)(scroll_back / USABLE_ROWS) + 1;
    uint32_t pg_total = (uint32_t)((max_back + USABLE_ROWS - 1) / USABLE_ROWS);
    if (pg_total == 0)
        pg_total = 1;
    if (pg_now > pg_total)
        pg_now = pg_total;

    char right[12];
    int ri = 0;
    right[ri++] = 'p';
    right[ri++] = 'g';
    right[ri++] = ' ';
    {
        char tmp[4];
        int ti = 0;
        uint32_t v = pg_now;
        do
        {
            tmp[ti++] = '0' + (v % 10);
            v /= 10;
        } while (v);
        while (ti > 0)
            right[ri++] = tmp[--ti];
    }
    right[ri++] = '/';
    {
        char tmp[4];
        int ti = 0;
        uint32_t v = pg_total;
        do
        {
            tmp[ti++] = '0' + (v % 10);
            v /= 10;
        } while (v);
        while (ti > 0)
            right[ri++] = tmp[--ti];
    }
    right[ri++] = ' ';

    size_t start = VGA_X - (size_t)ri;
    for (int j = 0; j < ri; j++)
        terminal_buffer[base + start + (size_t)j]
            = vga_entry ((uint8_t)right[j], statusbar_color);
}

static void
statusbar_draw (void)
{
    statusbar_draw_at (vga_view_top + USABLE_ROWS);
}

static void
vga_recycle (void)
{
    /* Keep half the buffer as scrollback history instead of discarding all of
     * it.  We copy (keep + USABLE_ROWS) rows starting from (vga_view_top -
     * keep) to the beginning, leaving ~VGA_MAX_VIEW_TOP/2 rows free for
     * future scrolling before the next recycle. */
    size_t keep = SCROLLBACK_ROWS;
    if (keep > vga_view_top)
        keep = vga_view_top;

    size_t src = vga_view_top - keep;
    size_t total = keep + USABLE_ROWS;
    for (size_t r = 0; r < total; r++)
        for (size_t c = 0; c < VGA_X; c++)
            terminal_buffer[r * VGA_X + c]
                = terminal_buffer[(src + r) * VGA_X + c];
    terminal_row -= src;
    vga_view_top = keep;
    scroll_back = 0;
    saved_bar_valid = 0;
}

static void
terminal_scroll (void)
{
    if (vga_view_top == VGA_MAX_VIEW_TOP)
        vga_recycle ();

    vga_view_top++;
    size_t new_bottom = vga_view_top + USABLE_ROWS - 1;
    for (size_t x = 0; x < VGA_X; x++)
        terminal_buffer[new_bottom * VGA_X + x]
            = vga_entry (' ', terminal_color);

    if (scroll_back == 0)
        vga_set_start (vga_view_top);

    statusbar_draw ();
}

/* Reset the screen state and repaint a blank view. Keeps the current
 * statusbar_color so the bar palette stays stable across clears. */
void
terminal_clear (void)
{
    terminal_row = 0;
    terminal_column = 0;
    vga_view_top = 0;
    scroll_back = 0;
    saved_bar_valid = 0;
    terminal_color = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = (volatile uint16_t *)0xB8000;

    for (size_t y = 0; y < USABLE_ROWS; y++)
    {
        for (size_t x = 0; x < VGA_X; x++)
        {
            terminal_buffer[y * VGA_X + x] = vga_entry (' ', terminal_color);
        }
    }

    vga_set_start (0);
    statusbar_draw ();
    terminal_update_cursor ();
}

/* One-time boot init: pick the status bar palette once, then clear. */
void
terminal_initialize (void)
{
    if (rdtsc () & 1)
    {
        statusbar_color = vga_entry_color (VGA_COLOR_WHITE, VGA_COLOR_RED);
    }
    else
    {
        statusbar_color = vga_entry_color (VGA_COLOR_RED, VGA_COLOR_WHITE);
    }

    terminal_clear ();
}

void
terminal_setcolor (uint8_t color)
{
    terminal_color = color;
}

uint8_t
terminal_getcolor (void)
{
    return terminal_color;
}

void
terminal_putentryat (char c, uint8_t color, size_t x, size_t y)
{
    terminal_buffer[y * VGA_X + x] = vga_entry ((uint8_t)c, color);
}

void
terminal_putchar (char c)
{
    if (c == '\n')
    {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row == vga_view_top + USABLE_ROWS)
            terminal_scroll ();
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
    terminal_putentryat ((uint8_t)c, terminal_color, terminal_column,
                         terminal_row);
    if (++terminal_column == VGA_X)
    {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row == vga_view_top + USABLE_ROWS)
            terminal_scroll ();
    }
    terminal_update_cursor ();
    statusbar_draw ();
}

void
terminal_write (const char *data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        terminal_putchar (data[i]);
}

void
terminal_writestring (const char *data)
{
    terminal_write (data, strlen (data));
}

static void
scrollback_save_and_draw (size_t bar_row)
{
    for (size_t x = 0; x < VGA_X; x++)
        saved_bar[x] = terminal_buffer[bar_row * VGA_X + x];
    saved_bar_row = bar_row;
    saved_bar_valid = 1;
    scrollbar_draw_at (bar_row);
}

static void
scrollback_restore (void)
{
    if (saved_bar_valid)
    {
        for (size_t x = 0; x < VGA_X; x++)
            terminal_buffer[saved_bar_row * VGA_X + x] = saved_bar[x];
        saved_bar_valid = 0;
    }
}

void
terminal_scroll_lines (int delta)
{
    if (delta < 0)
    {
        size_t amount = (size_t)(-delta);
        size_t max_back
            = vga_view_top < SCROLLBACK_ROWS ? vga_view_top : SCROLLBACK_ROWS;
        scroll_back = (scroll_back + amount > max_back) ? max_back
                                                        : scroll_back + amount;
    }
    else if (delta > 0)
    {
        size_t amount = (size_t)delta;
        scroll_back = (amount >= scroll_back) ? 0 : scroll_back - amount;
    }

    /* restore any previously saved row before repositioning the bar */
    scrollback_restore ();

    if (scroll_back > 0)
    {
        size_t bar_row = vga_view_top - scroll_back + USABLE_ROWS;
        scrollback_save_and_draw (bar_row);
    }
    else
    {
        /* back to normal: redraw status bar at its usual position */
        statusbar_draw ();
    }

    vga_set_start (vga_view_top - scroll_back);
    terminal_update_cursor ();
}

void
terminal_scroll_reset (void)
{
    if (scroll_back > 0)
    {
        scrollback_restore ();
        scroll_back = 0;
        statusbar_draw ();
        vga_set_start (vga_view_top);
        terminal_update_cursor ();
    }
}
