/* SPDX-License-Identifier: MIT */
/* lib/mk_stdio.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

static char obuf[80];
static u8 olen;

int fflush(void)
{
    u8 n = olen;
    olen = 0;
    if (n) write(1, obuf, n);
    return 0;
}

int putchar(int c)
{
    obuf[olen++] = (char)c;
    if (c == '\n' || olen >= sizeof(obuf)) fflush();
    return c;
}

int puts(const char *s)
{
    while (*s) putchar(*s++);
    putchar('\n');
    return 0;
}

int putstr(const char *s)
{
    while (*s) putchar(*s++);
    return 0;
}

