/* SPDX-License-Identifier: MIT */
/* lib/mk_rename.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int rename(const char *from, const char *to)
{
    msg_t m;
    char both[2 * MAXPATH];
    u16 a = (u16)strlen(from), b = (u16)strlen(to);
    if (a >= MAXPATH || b >= MAXPATH) return -E_NAME;
    memcpy(both, from, a + 1);
    memcpy(both + a + 1, to, b + 1);
    m.op = FS_RENAME;
    m.b = (u16)both;
    m.c = a + b + 2;
    return mk_rpc(TID_FS, &m, GR_READ);
}

