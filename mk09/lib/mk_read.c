/* SPDX-License-Identifier: MIT */
/* lib/mk_read.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int read(int fd, void *buf, u16 n)
{
    msg_t m;
    int r;
    if (fd == 0 && in_h != NOFD) fd = in_h + 3;
    m.b = (u16)buf;
    m.c = n;
    if (fd < 3) {
        fflush();
        m.op = TTY_READ;
        r = mk_rpc(TID_TTY, &m, GR_WRITE);
    } else {
        m.op = FS_READ;
        m.a = fd - 3;
        r = mk_rpc(TID_FS, &m, GR_WRITE);
    }
    if (r) return r;
    return (int)m.a;
}

