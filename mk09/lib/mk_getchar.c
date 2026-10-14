/* SPDX-License-Identifier: MIT */
/* lib/mk_getchar.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int getchar(void)
{
    u8 c;
    if (read(0, &c, 1) != 1) return -1;
    return c;
}

int gets_(char *buf, u16 n)
{
    int r = read(0, buf, n - 1);
    if (r <= 0) return -1;
    if (buf[r - 1] == '\n') r--;
    buf[r] = 0;
    return r;
}

