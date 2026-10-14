/* SPDX-License-Identifier: MIT */
/* lib/mk_ttymode.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
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
/* console                                                             */
/* ------------------------------------------------------------------ */
int tty_getmode(u8 *mode, u8 *charset, u8 *cols, u8 *rows)
{
    msg_t m;
    int r;
    m.op = TTY_IOCTL;
    m.a = TTYC_GETMODE;
    r = mk_rpc(TID_TTY, &m, 0);
    if (r) return r;
    if (mode) *mode = (u8)m.a;
    if (charset) *charset = (u8)m.b;
    if (cols) *cols = (u8)m.c;
    if (rows) *rows = (u8)m.d;
    return 0;
}

int tty_setmode(u8 mode, u8 charset, u8 cols, u8 rows)
{
    msg_t m;
    m.op = TTY_IOCTL;
    m.a = TTYC_SETMODE;
    m.b = mode;
    m.c = charset == 0xFF ? 0xFFFF : charset;
    m.d = cols == 0xFF ? 0xFFFF : ((u16)cols << 8) | rows;
    return mk_rpc(TID_TTY, &m, 0);
}

