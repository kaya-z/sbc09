/* SPDX-License-Identifier: MIT */
/* lib/mk_statfs.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int statfs(u8 *spc, u16 *clusters, u16 *freec)
{
    msg_t m;
    int r;
    m.op = FS_STATFS;
    r = mk_rpc(TID_FS, &m, 0);
    if (r) return r;
    *spc = (u8)m.a;
    *clusters = m.b;
    *freec = m.c;
    return 0;
}

