/* SPDX-License-Identifier: MIT */
/* lib/mk_chdir.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int chdir(const char *path)
{
    msg_t m;
    return mk_rpc_path(FS_CHDIR, path, 0, &m);
}

