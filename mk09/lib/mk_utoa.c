/* SPDX-License-Identifier: MIT */
/* lib/mk_utoa.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

u16 utoa_(u16 v, char *s, u8 base)
{
    char t[6];
    u8 n = 0, i = 0;
    u16 d;
    do {
        d = v % base;
        t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    while (n) s[i++] = t[--n];
    s[i] = 0;
    return i;
}

