#include "gdt.h"
#include "idt.h"
#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "klib.h"
#include "kmalloc.h"
#include "kmem_dyn_alloc.h"
#include "kthreads_test.h"
#include "paging.h"
#include "phys_page_frame.h"
#include "pic.h"
#include "pit.h"
#include "printk.h"
#include "signal.h"
#include "task.h"
#include "trap_frame.h"
#include "tss.h"
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

void shell_print_prompt (void);

/* eyeproc: full-screen process grid monitor
 * 4 cols x 3 rows = 12 cells/page, each cell 20x7 chars
 * rows 0-20: grid, rows 21-23: status area */

#define EP_COLS 4
#define EP_ROWS 3
#define EP_CELL_W 20
#define EP_CELL_H 7
#define EP_INNER_W 18
#define EP_PER_PAGE (EP_COLS * EP_ROWS)

static void
ep_put (size_t x, size_t y, char c, uint8_t color)
{
    terminal_putentryat (c, color, x, y);
}

static void
ep_puts (size_t x, size_t y, const char *s, uint8_t color, int width)
{
    int i = 0;
    for (; s[i] && i < width; i++)
    {
        terminal_putentryat (s[i], color, x + (size_t)i, y);
    }
    for (; i < width; i++)
    {
        terminal_putentryat (' ', color, x + (size_t)i, y);
    }
}

static int
ep_fmt_uint (char *buf, uint32_t v)
{
    if (!v)
    {
        buf[0] = '0';
        return 1;
    }
    char tmp[10];
    int n = 0;
    while (v)
    {
        tmp[n++] = '0' + (int)(v % 10);
        v /= 10;
    }
    for (int i = 0; i < n; i++)
    {
        buf[i] = tmp[n - 1 - i];
    }
    return n;
}

static void
ep_fmt_hex8 (char *buf, uint32_t v)
{
    for (int i = 7; i >= 0; i--)
    {
        buf[i] = "0123456789abcdef"[v & 0xF];
        v >>= 4;
    }
}

static const char *
ep_state_name (enum task_state s)
{
    switch (s)
    {
        case TASK_RUNNABLE:
            return "RUNNABLE";
        case TASK_RUNNING:
            return "RUNNING ";
        case TASK_BLOCKED:
            return "BLOCKED ";
        case TASK_ZOMBIE:
            return "ZOMBIE  ";
        default:
            return "UNKNOWN ";
    }
}

static uint8_t
ep_state_color (enum task_state s)
{
    switch (s)
    {
        case TASK_RUNNABLE:
            return vga_entry_color (VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        case TASK_RUNNING:
            return vga_entry_color (VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        case TASK_BLOCKED:
            return vga_entry_color (VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        case TASK_ZOMBIE:
            return vga_entry_color (VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        default:
            return vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}

static void
ep_draw_cell (int gc, int gr, struct task *t)
{
    size_t x0 = (size_t)(gc * EP_CELL_W);
    size_t y0 = (size_t)(gr * EP_CELL_H);

    uint8_t bc = t ? vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK)
                   : vga_entry_color (VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);

    /* top and bottom borders */
    for (int edge = 0; edge < 2; edge++)
    {
        size_t ey = edge ? y0 + EP_CELL_H - 1 : y0;
        ep_put (x0, ey, '+', bc);
        for (int i = 1; i < EP_CELL_W - 1; i++)
        {
            ep_put (x0 + (size_t)i, ey, '-', bc);
        }
        ep_put (x0 + EP_CELL_W - 1, ey, '+', bc);
    }

    /* side borders + blank inner rows */
    for (int dr = 1; dr < EP_CELL_H - 1; dr++)
    {
        ep_put (x0, y0 + (size_t)dr, '|', bc);
        ep_puts (x0 + 1, y0 + (size_t)dr, "", bc, EP_INNER_W);
        ep_put (x0 + EP_CELL_W - 1, y0 + (size_t)dr, '|', bc);
    }

    if (!t)
    {
        return;
    }

    uint8_t text = vga_entry_color (VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t dim = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    uint8_t addr = vga_entry_color (VGA_COLOR_LIGHT_BROWN, VGA_COLOR_BLACK);

    /* row 1: PID */
    {
        char line[EP_INNER_W + 1];
        line[0] = 'P';
        line[1] = 'I';
        line[2] = 'D';
        line[3] = ':';
        line[4] = ' ';
        int n = ep_fmt_uint (line + 5, t->pid);
        line[5 + n] = '\0';
        ep_puts (x0 + 1, y0 + 1, line, text, EP_INNER_W);
    }

    /* row 2: State — label in dim, value in state colour */
    ep_puts (x0 + 1, y0 + 2, "State: ", dim, 7);
    ep_puts (x0 + 8, y0 + 2, ep_state_name (t->state),
             ep_state_color (t->state), EP_INNER_W - 7);

    /* row 3: UID */
    {
        char line[EP_INNER_W + 1];
        line[0] = 'U';
        line[1] = 'I';
        line[2] = 'D';
        line[3] = ':';
        line[4] = ' ';
        int n = ep_fmt_uint (line + 5, t->uid);
        line[5 + n] = '\0';
        ep_puts (x0 + 1, y0 + 3, line, dim, EP_INNER_W);
    }

    /* row 4: code start */
    {
        char line[EP_INNER_W + 1];
        line[0] = 'C';
        line[1] = 'o';
        line[2] = 'd';
        line[3] = ':';
        line[4] = ' ';
        line[5] = '0';
        line[6] = 'x';
        ep_fmt_hex8 (line + 7, t->mm.code_start);
        line[15] = '\0';
        ep_puts (x0 + 1, y0 + 4, line, addr, EP_INNER_W);
    }

    /* row 5: stack top */
    {
        char line[EP_INNER_W + 1];
        line[0] = 'S';
        line[1] = 't';
        line[2] = 'k';
        line[3] = ':';
        line[4] = ' ';
        line[5] = '0';
        line[6] = 'x';
        ep_fmt_hex8 (line + 7, t->mm.stack_top);
        line[15] = '\0';
        ep_puts (x0 + 1, y0 + 5, line, addr, EP_INNER_W);
    }
}

/* write a string into the bar row at column cx, advance cx, return new cx */
static size_t
ep_bar_str (const char *s, uint8_t color, size_t cx, size_t row)
{
    while (*s)
    {
        terminal_putentryat (*s++, color, cx++, row);
    }
    return cx;
}

/* write a uint32 into the bar row, return new cx */
static size_t
ep_bar_uint (uint32_t v, uint8_t color, size_t cx, size_t row)
{
    char buf[12];
    int n = ep_fmt_uint (buf, v);
    for (int i = 0; i < n; i++)
    {
        terminal_putentryat (buf[i], color, cx++, row);
    }
    return cx;
}

static void
ep_draw_statusbar (int page, int total_pages, int cnt_run, int cnt_runnable,
                   int cnt_blocked, int cnt_zombie, int nprocs)
{
    uint8_t bar = vga_entry_color (VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    uint8_t hi = vga_entry_color (VGA_COLOR_GREEN, VGA_COLOR_LIGHT_GREY);
    uint8_t blk = vga_entry_color (VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    uint8_t crun
        = vga_entry_color (VGA_COLOR_LIGHT_GREEN, VGA_COLOR_LIGHT_GREY);
    uint8_t crbl = vga_entry_color (VGA_COLOR_LIGHT_CYAN, VGA_COLOR_LIGHT_GREY);
    uint8_t cblk = vga_entry_color (VGA_COLOR_LIGHT_RED, VGA_COLOR_LIGHT_GREY);
    uint8_t czmb = vga_entry_color (VGA_COLOR_DARK_GREY, VGA_COLOR_LIGHT_GREY);

    /* row 21: separator */
    for (size_t x = 0; x < 80; x++)
    {
        ep_put (x, 21, ' ', blk);
    }

    /* rows 22 and 23: bar background */
    for (size_t x = 0; x < 80; x++)
    {
        ep_put (x, 22, ' ', bar);
        ep_put (x, 23, ' ', bar);
    }

    /* row 24: overwrite VGA native bar with a dark blank */
    for (size_t x = 0; x < 80; x++)
    {
        ep_put (x, 24, ' ', blk);
    }

    /* --- row 22: navigation --- */
    size_t cx = 1;
    cx = ep_bar_str ("eyeproc", hi, cx, 22);
    cx = ep_bar_str ("  |  pg ", bar, cx, 22);
    cx = ep_bar_uint ((uint32_t)(page + 1), bar, cx, 22);
    terminal_putentryat ('/', bar, cx++, 22);
    cx = ep_bar_uint ((uint32_t)total_pages, bar, cx, 22);
    cx = ep_bar_str ("  |  UP prev   DOWN next   ESC quit", bar, cx, 22);

    /* --- row 23: process statistics --- */
    cx = 1;
    cx = ep_bar_str ("procs: ", bar, cx, 23);
    cx = ep_bar_uint ((uint32_t)nprocs, bar, cx, 23);
    cx = ep_bar_str ("   running: ", bar, cx, 23);
    cx = ep_bar_uint ((uint32_t)cnt_run, crun, cx, 23);
    cx = ep_bar_str ("   runnable: ", bar, cx, 23);
    cx = ep_bar_uint ((uint32_t)cnt_runnable, crbl, cx, 23);
    cx = ep_bar_str ("   blocked: ", bar, cx, 23);
    cx = ep_bar_uint ((uint32_t)cnt_blocked, cblk, cx, 23);
    cx = ep_bar_str ("   zombie: ", bar, cx, 23);
    cx = ep_bar_uint ((uint32_t)cnt_zombie, czmb, cx, 23);
    (void)cx;

    /* anchor the blinking cursor at the start of the navigation bar */
    terminal_set_cursor (1, 22);
}

static void
ep_render (int page, struct task **procs, int nprocs, int total_pages)
{
    int cnt_run = 0, cnt_runnable = 0, cnt_blocked = 0, cnt_zombie = 0;
    for (int i = 0; i < nprocs; i++)
    {
        switch (procs[i]->state)
        {
            case TASK_RUNNING:
                cnt_run++;
                break;
            case TASK_RUNNABLE:
                cnt_runnable++;
                break;
            case TASK_BLOCKED:
                cnt_blocked++;
                break;
            case TASK_ZOMBIE:
                cnt_zombie++;
                break;
            default:
                break;
        }
    }

    for (int gr = 0; gr < EP_ROWS; gr++)
    {
        for (int gc = 0; gc < EP_COLS; gc++)
        {
            int idx = page * EP_PER_PAGE + gr * EP_COLS + gc;
            ep_draw_cell (gc, gr, (idx < nprocs) ? procs[idx] : NULL);
        }
    }

    ep_draw_statusbar (page, total_pages, cnt_run, cnt_runnable, cnt_blocked,
                       cnt_zombie, nprocs);
}

/* per-slot code buffers: each pidN_fn gets copied here by exec_fn before the
 * task starts running. Page-aligned because the buffer doubles as the code
 * page; one buffer = one slot, indexed 1..6. */
#define MAX_TEST_PROCS 6
static uint8_t proc_code_buf[MAX_TEST_PROCS][PAGE_SIZE]
    __attribute__ ((aligned (PAGE_SIZE)));

/* dedicated buffer for the fork driver, separate from the numbered spawn slots
 */
static uint8_t forker_code_buf[PAGE_SIZE] __attribute__ ((aligned (PAGE_SIZE)));

void
shell_spawnproc (int slot)
{
    if (slot < 1 || slot > MAX_TEST_PROCS)
    {
        terminal_writestring ("spawnproc: slot must be 1..6\n");
        return;
    }

    void (*fn) (void) = NULL;
    void (*fn_end) (void) = NULL;
    switch (slot)
    {
        case 1:
            fn = kth_heartbeat_fn;
            fn_end = kth_heartbeat_fn_end;
            break;
        case 2:
            fn = kth_compute_fn;
            fn_end = kth_compute_fn_end;
            break;
        case 3:
            fn = kth_memwrite_fn;
            fn_end = kth_memwrite_fn_end;
            break;
        case 4:
            fn = kth_memread_fn;
            fn_end = kth_memread_fn_end;
            break;
        case 5:
            fn = kth_slow_fn;
            fn_end = kth_slow_fn_end;
            break;
        case 6:
            fn = kth_idle_fn;
            fn_end = kth_idle_fn_end;
            break;
    }

    uint32_t size = (uint32_t)fn_end - (uint32_t)fn;
    if (size == 0 || size > PAGE_SIZE)
    {
        terminal_writestring ("spawnproc: invalid function size\n");
        return;
    }

    exec_fn ((uint32_t *)proc_code_buf[slot - 1], (uint32_t *)fn, size);
    terminal_writestring ("spawnproc: launched\n");
}

void
shell_eyeproc (void)
{
    struct task *procs[MAX_PROC];
    int nprocs = 0;

    for (struct task *t = task_list_head; t && nprocs < MAX_PROC; t = t->next)
    {
        procs[nprocs++] = t;
    }

    int total_pages = (nprocs + EP_PER_PAGE - 1) / EP_PER_PAGE;
    if (total_pages < 1)
    {
        total_pages = 1;
    }

    int page = 0;

    disable_interrupts ();
    terminal_initialize ();
    ep_render (page, procs, nprocs, total_pages);

    bool extended = false;
    bool quit = false;
    while (!quit)
    {
        if (!(inb (0x64) & 1))
        {
            continue;
        }

        uint8_t sc = inb (0x60);

        if (sc == 0xE0)
        {
            extended = true;
            continue;
        }

        if (extended)
        {
            extended = false;
            if (!(sc & 0x80)) /* key press only */
            {
                if (sc == 0x48 && page > 0) /* UP arrow */
                {
                    page--;
                    ep_render (page, procs, nprocs, total_pages);
                }
                else if (sc == 0x50 && page < total_pages - 1) /* DOWN arrow */
                {
                    page++;
                    ep_render (page, procs, nprocs, total_pages);
                }
            }
            continue;
        }

        if (!(sc & 0x80) && sc == 0x01) /* ESC */
        {
            quit = true;
        }
    }

    terminal_initialize ();
    shell_print_prompt ();
    enable_interrupts ();
}

/* returns len(prefix) if cmd starts with prefix, else 0 */
static size_t
shell_starts_with (const char *cmd, const char *prefix)
{
    size_t i = 0;
    while (prefix[i] != '\0')
    {
        if (cmd[i] != prefix[i])
        {
            return 0;
        }
        i++;
    }
    return i;
}

/* parse base-10 uint at *p, advance past digits; 1 on success, 0 if no digit */
static int
shell_parse_uint (const char **p, uint32_t *out)
{
    const char *s = *p;
    if (*s < '0' || *s > '9')
    {
        return 0;
    }
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    *p = s;
    *out = v;
    return 1;
}

static void
shell_skip_spaces (const char **p)
{
    while (**p == ' ')
    {
        (*p)++;
    }
}

static struct task *
shell_find_task (uint32_t pid)
{
    for (struct task *t = task_list_head; t != NULL; t = t->next)
    {
        if (t->pid == pid)
        {
            return t;
        }
    }
    return NULL;
}

/* debug handler installed by the `signal` command — logs on delivery */
static void
shell_debug_sig_handler (int signo)
{
    pr_info ("signal handler fired: sig=%d pid=%u\n", signo, current_task->pid);
}

/* shell-side kill: bypasses int $0x80 (shell runs as init in ring 0) */
static void
shell_kill (uint32_t pid, int signum)
{
    if (signum <= 0 || signum >= NSIG)
    {
        terminal_writestring ("kill: invalid signal\n");
        return;
    }
    struct task *t = shell_find_task (pid);
    if (!t)
    {
        terminal_writestring ("kill: pid not found\n");
        return;
    }
    kernel_signal_send (t, signum);
    pr_info ("kill: sent sig %d to pid %u\n", signum, pid);
}

/* installs shell_debug_sig_handler on pid for signum, for hand-testing kill */
static void
shell_signal (uint32_t pid, int signum)
{
    if (signum <= 0 || signum >= NSIG)
    {
        terminal_writestring ("signal: invalid signal\n");
        return;
    }
    struct task *t = shell_find_task (pid);
    if (!t)
    {
        terminal_writestring ("signal: pid not found\n");
        return;
    }
    kernel_signal_register (t, signum, shell_debug_sig_handler);
    pr_info ("signal: installed debug handler for sig %d on pid %u\n", signum,
             pid);
}

static void
shell_forktest (void)
{
    uint32_t size = (uint32_t)forker_fn_end - (uint32_t)forker_fn;
    if (size == 0 || size > PAGE_SIZE)
    {
        terminal_writestring ("forktest: invalid function size\n");
        return;
    }
    exec_fn ((uint32_t *)forker_code_buf, (uint32_t *)forker_fn, size);
    terminal_writestring ("forktest: launched\n");
}

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

static void
shell_momentum (void)
{
    struct task *t = current_task;
    uint32_t live_esp = get_esp ();

    terminal_writestring ("#### current execution context ####\n");

    terminal_writestring ("pid:        ");
    terminal_write_uint (t->pid);
    terminal_putchar ('\n');

    if (t->parent)
    {
        terminal_writestring ("ppid:       ");
        terminal_write_uint (t->parent->pid);
        terminal_putchar ('\n');
    }

    terminal_writestring ("uid:        ");
    terminal_write_uint (t->uid);
    terminal_writestring ("  euid: ");
    terminal_write_uint (t->euid);
    terminal_putchar ('\n');

    const char *state_str;
    switch (t->state)
    {
        case TASK_RUNNABLE:
            state_str = "RUNNABLE";
            break;
        case TASK_RUNNING:
            state_str = "RUNNING";
            break;
        case TASK_BLOCKED:
            state_str = "BLOCKED";
            break;
        case TASK_ZOMBIE:
            state_str = "ZOMBIE";
            break;
        default:
            state_str = "UNKNOWN";
            break;
    }
    terminal_writestring ("state:      ");
    terminal_writestring (state_str);
    terminal_putchar ('\n');

    terminal_writestring ("quantum:    ");
    terminal_write_uint (t->time_left);
    terminal_writestring (" / ");
    terminal_write_uint (t->quantum);
    terminal_writestring (" ticks\n");

    terminal_writestring ("thread.esp: ");
    terminal_write_hex (t->thread.esp);
    terminal_putchar ('\n');

    terminal_writestring ("live esp:   ");
    terminal_write_hex (live_esp);
    terminal_putchar ('\n');

    terminal_writestring ("thread.tf:  ");
    if (t->thread.tf)
    {
        terminal_write_hex ((uint32_t)t->thread.tf);
    }
    else
    {
        terminal_writestring ("(null, not preempted)");
    }
    terminal_putchar ('\n');

    terminal_writestring ("cr3:        ");
    terminal_write_hex (t->mm.pgdir);
    terminal_putchar ('\n');

    terminal_writestring ("code:       ");
    terminal_write_hex (t->mm.code_start);
    terminal_writestring (" - ");
    terminal_write_hex (t->mm.code_end);
    terminal_putchar ('\n');

    terminal_writestring ("stack_top:  ");
    terminal_write_hex (t->mm.stack_top);
    terminal_putchar ('\n');
}

static void
shell_help (void)
{
    terminal_writestring ("available commands:\n");
    terminal_writestring ("  help     - show this help message\n");
    terminal_writestring ("  clear    - clear the terminal screen\n");
    terminal_writestring ("  momentum - dump current task execution context\n");
    terminal_writestring ("  dmesg    - display kernel ring buffer\n");
    terminal_writestring ("  memdump  - display memory usage summary\n");
    terminal_writestring ("  reboot   - reboot the system\n");
    terminal_writestring ("  shutdown - power off the system (ACPI)\n");
    terminal_writestring ("  halt     - halt the CPU\n");
    terminal_writestring (
        "  traptest - trigger INT 0x42 and dump the trap frame\n");
    terminal_writestring ("  keymap   - qwerty | azerty\n");
    terminal_writestring (
        "  eyeproc  - full-screen process grid (ESC to quit)\n");
    terminal_writestring (
        "  spawnproc [N] - launch test process N (1..6), or all if omitted\n");
    terminal_writestring (
        "  forktest - run a fork+wait+exit demo via int 0x80\n");
    terminal_writestring ("  kill <pid> <sig>   - send signal sig to pid\n");
    terminal_writestring (
        "  signal <pid> <sig> - install a debug handler on pid for sig\n");
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
    else if (strcmp (cmd, "clear") == 0)
    {
        terminal_initialize ();
    }
    else if (strcmp (cmd, "momentum") == 0)
    {
        shell_momentum ();
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
    else if (strcmp (cmd, "eyeproc") == 0)
    {
        shell_eyeproc ();
        return;
    }
    else if (strcmp (cmd, "spawnproc") == 0)
    {
        for (int i = 1; i <= MAX_TEST_PROCS; i++)
        {
            shell_spawnproc (i);
        }
    }
    else if (cmd[0] == 's' && cmd[1] == 'p' && cmd[2] == 'a' && cmd[3] == 'w'
             && cmd[4] == 'n' && cmd[5] == 'p' && cmd[6] == 'r' && cmd[7] == 'o'
             && cmd[8] == 'c' && cmd[9] == ' ' && cmd[10] >= '0'
             && cmd[10] <= '9' && cmd[11] == '\0')
    {
        shell_spawnproc (cmd[10] - '0');
    }
    else if (strcmp (cmd, "forktest") == 0)
    {
        shell_forktest ();
    }
    else if (shell_starts_with (cmd, "kill ") > 0)
    {
        const char *p = cmd + 5;
        shell_skip_spaces (&p);
        uint32_t pid = 0, sig = 0;
        if (!shell_parse_uint (&p, &pid))
        {
            terminal_writestring ("kill: usage: kill <pid> <sig>\n");
        }
        else
        {
            shell_skip_spaces (&p);
            if (!shell_parse_uint (&p, &sig))
            {
                terminal_writestring ("kill: usage: kill <pid> <sig>\n");
            }
            else
            {
                shell_kill (pid, (int)sig);
            }
        }
    }
    else if (shell_starts_with (cmd, "signal ") > 0)
    {
        const char *p = cmd + 7;
        shell_skip_spaces (&p);
        uint32_t pid = 0, sig = 0;
        if (!shell_parse_uint (&p, &pid))
        {
            terminal_writestring ("signal: usage: signal <pid> <sig>\n");
        }
        else
        {
            shell_skip_spaces (&p);
            if (!shell_parse_uint (&p, &sig))
            {
                terminal_writestring ("signal: usage: signal <pid> <sig>\n");
            }
            else
            {
                shell_signal (pid, (int)sig);
            }
        }
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
    /* TSS lives at GDT entry 7 (already installed by gdt_init); we now zero
     * its body, set ss0, and load TR via ltr $0x38. From this point on the
     * CPU has a valid kernel-stack pointer for Ring3 -> Ring0 transitions. */
    tss_init ();
    tss_flush ();
    pic_remap ();
    idt_init ();
    pit_init (PIT_TICK_HZ);
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

    task_init ();

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
