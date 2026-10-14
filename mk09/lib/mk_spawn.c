/* SPDX-License-Identifier: MIT */
/* lib/mk_spawn.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
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
/* processes                                                           */
/* ------------------------------------------------------------------ */
int spawn(const char *block, u16 len, u16 in_h_, u16 out_h_)
{
    msg_t m;
    int r;
    fflush();
    m.op = PM_SPAWN;
    m.a = in_h_;
    m.d = out_h_;
    m.b = (u16)block;
    m.c = len;
    r = mk_rpc(TID_PM, &m, GR_READ);
    if (r) return r;
    return (int)m.a;
}

