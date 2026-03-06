#ifndef KPANIC_H
#define KPANIC_H

#define VGA_PANIC_ADDR   0xB8000
#define VGA_PANIC_COLS   80
#define VGA_PANIC_ROWS   25
#define VGA_PANIC_COLOR  0x4F /* white on red */

void kpanic (const char *msg);

#endif
