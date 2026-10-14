/* SPDX-License-Identifier: MIT */
/* lib/mk_rpc.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define NOFD 0xFF

/* ------------------------------------------------------------------ */
/* messages                                                            */
/* ------------------------------------------------------------------ */
int mk_rpc(u8 dst, msg_t *m, u8 gfl)
{
    u8 st = gfl ? mk_callg(dst, m, gfl) : mk_call(dst, m);
    if (st) return -(int)st;
    if (m->op) return -(int)m->op;
    return 0;
}

int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m)
{
    m->op = op;
    m->a = a;
    m->b = (u16)path;
    m->c = (u16)strlen(path) + 1;
    return mk_rpc(TID_FS, m, GR_READ);
}

