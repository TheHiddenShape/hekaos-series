#include "../../include/klib.h"
#include "../../include/printk.h"

static char log_buf[LOG_BUF_SIZE];
static size_t log_write_idx = 0;
static size_t log_size = 0;

static uint64_t boot_tsc = 0;
static int timestamps_enabled = 0;

static size_t console_read_idx = 0;

#define HEX_DIGITS "0123456789abcdef"

static void
log_buf_putchar (char c)
{
    log_buf[log_write_idx] = c;
    log_write_idx = (log_write_idx + 1) % LOG_BUF_SIZE;
    if (log_size < LOG_BUF_SIZE)
        log_size++;
}

static void
printk_putchar (char c)
{
    log_buf_putchar (c);
}

static void
print_string (const char *str)
{
    while (*str)
    {
        printk_putchar (*str++);
    }
}

static void
print_int (int value, int base, int is_signed)
{
    char buffer[32];
    char *ptr = buffer + sizeof (buffer) - 1;
    int negative = 0;
    uint32_t uvalue;

    *ptr = '\0';

    if (is_signed && value < 0)
    {
        negative = 1;
        uvalue = (uint32_t)(-(value + 1)) + 1;
    }
    else
    {
        uvalue = (uint32_t)value;
    }

    if (uvalue == 0)
    {
        *--ptr = '0';
    }
    else
    {
        const char *digits = HEX_DIGITS;
        while (uvalue > 0)
        {
            *--ptr = digits[uvalue % base];
            uvalue /= base;
        }
    }

    if (negative)
    {
        *--ptr = '-';
    }

    print_string (ptr);
}

static void
print_hex (uint32_t value)
{
    print_string ("0x");
    if (value == 0)
    {
        printk_putchar ('0');
        return;
    }

    char buffer[16];
    char *ptr = buffer + sizeof (buffer) - 1;
    *ptr = '\0';

    const char *digits = "0123456789abcdef";
    while (value > 0)
    {
        *--ptr = digits[value % 16];
        value /= 16;
    }

    print_string (ptr);
}

int
printk (const char *format, ...)
{
    va_list args;
    int count = 0;

    va_start (args, format);

    while (*format)
    {
        if (*format == '%')
        {
            format++;
            switch (*format)
            {
            case 's':
            {
                const char *str = va_arg (args, const char *);
                if (str == NULL)
                    str = "(null)";
                print_string (str);
                count += strlen (str);
                break;
            }
            case 'd':
            case 'i':
            {
                int val = va_arg (args, int);
                print_int (val, 10, 1);
                count++;
                break;
            }
            case 'u':
            {
                uint32_t val = va_arg (args, uint32_t);
                print_int (val, 10, 0);
                count++;
                break;
            }
            case 'x':
            {
                uint32_t val = va_arg (args, uint32_t);
                print_hex (val);
                count++;
                break;
            }
            case 'p':
            {
                void *ptr = va_arg (args, void *);
                print_hex ((uint32_t)ptr);
                count++;
                break;
            }
            case 'c':
            {
                char c = (char)va_arg (args, int);
                printk_putchar (c);
                count++;
                break;
            }
            case '%':
                printk_putchar ('%');
                count++;
                break;
            default:
                printk_putchar ('%');
                printk_putchar (*format);
                count += 2;
                break;
            }
        }
        else
        {
            printk_putchar (*format);
            count++;
        }
        format++;
    }

    va_end (args);
    return count;
}

void
dmesg (void)
{
    size_t start;
    size_t count;

    if (log_size < LOG_BUF_SIZE)
    {
        start = 0;
        count = log_write_idx;
    }
    else
    {
        start = log_write_idx;
        count = LOG_BUF_SIZE;
    }

    for (size_t i = 0; i < count; i++)
    {
        size_t idx = (start + i) % LOG_BUF_SIZE;
        terminal_putchar (log_buf[idx]);
    }
}

size_t
get_log_buf (char *buf, size_t len)
{
    size_t start;
    size_t count;
    size_t copied = 0;

    if (log_size < LOG_BUF_SIZE)
    {
        start = 0;
        count = log_write_idx;
    }
    else
    {
        start = log_write_idx;
        count = LOG_BUF_SIZE;
    }

    for (size_t i = 0; i < count && copied < len; i++)
    {
        size_t idx = (start + i) % LOG_BUF_SIZE;
        buf[copied++] = log_buf[idx];
    }

    return copied;
}

void
console_flush (void)
{
    while (console_read_idx != log_write_idx)
    {
        terminal_putchar (log_buf[console_read_idx]);
        console_read_idx = (console_read_idx + 1) % LOG_BUF_SIZE;
    }
}
