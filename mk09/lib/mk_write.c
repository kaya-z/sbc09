/* SPDX-License-Identifier: MIT */
/* lib/mk_write.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int write(int fd, const void *buf, u16 n)
{
    msg_t m;
    int r;
    if (fd == 1 && out_h != NOFD) fd = out_h + 3;
    m.b = (u16)buf;
    m.c = n;
    if (fd < 3) {
        m.op = TTY_WRITE;
        r = mk_rpc(TID_TTY, &m, GR_READ);
    } else {
        m.op = FS_WRITE;
        m.a = fd - 3;
        r = mk_rpc(TID_FS, &m, GR_READ);
    }
    if (r) return r;
    return (int)m.a;
}

