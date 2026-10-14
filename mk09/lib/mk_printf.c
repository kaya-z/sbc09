/* SPDX-License-Identifier: MIT */
/* lib/mk_printf.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include <stdarg.h>

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

/* printf: %d %u %x %X %c %s %%, a width, '-' and '0' flags, 'l' is ignored for 16 bit values */
int printf(const char *fmt, ...)
{
    va_list ap;
    char num[8];
    const char *s;
    u8 left, zero, w, n, base, up, i;
    int v;
    u16 uv;
    int count = 0;

    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { putchar(*fmt++); count++; continue; }
        fmt++;
        left = zero = w = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') w = w * 10 + (*fmt++ - '0');
        if (*fmt == 'l') fmt++;
        s = num;
        up = 0;
        switch (*fmt) {
        case 'd':
            v = va_arg(ap, int);
            num[0] = 0;
            if (v < 0) { num[0] = '-'; uv = (u16)-v; }
            else uv = (u16)v;
            n = (u8)utoa_(uv, num + (num[0] ? 1 : 0), 10);
            if (num[0] == '-') n++;
            break;
        case 'u':
        case 'x':
        case 'X':
            uv = va_arg(ap, unsigned);
            base = *fmt == 'u' ? 10 : 16;
            up = *fmt == 'X';
            n = (u8)utoa_(uv, num, base);
            if (up)
                for (i = 0; i < n; i++)
                    if (num[i] >= 'a') num[i] -= 32;
            break;
        case 'c':
            num[0] = (char)va_arg(ap, int);
            num[1] = 0;
            n = 1;
            break;
        case 's':
            s = va_arg(ap, char *);
            if (!s) s = "(null)";
            n = (u8)strlen(s);
            break;
        default:
            putchar(*fmt ? *fmt : '%');
            if (*fmt) fmt++;
            count++;
            continue;
        }
        fmt++;
        if (!left)
            for (i = n; i < w; i++) { putchar(zero ? '0' : ' '); count++; }
        for (i = 0; i < n; i++) putchar(s[i]);
        count += n;
        if (left)
            for (i = n; i < w; i++) { putchar(' '); count++; }
    }
    va_end(ap);
    return count;
}

