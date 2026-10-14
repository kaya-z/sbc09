/* SPDX-License-Identifier: MIT */
/* lib/mk_open.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

/* ------------------------------------------------------------------ */
/* files                                                               */
/* ------------------------------------------------------------------ */
int open(const char *path, u8 flags)
{
    msg_t m;
    int r = mk_rpc_path(FS_OPEN, path, flags, &m);
    if (r) return r;
    return (int)m.a + 3;
}

int close(int fd)
{
    msg_t m;
    if (fd < 3) return 0;
    m.op = FS_CLOSE;
    m.a = fd - 3;
    return mk_rpc(TID_FS, &m, 0);
}

