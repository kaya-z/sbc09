/* SPDX-License-Identifier: MIT */
/* lib/mk_start.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include <stdarg.h>

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

extern int mk_main(int argc, char **argv);

void exit(int code)
{
    fflush();
    mk_exit((u8)code);
}

/* called by crt0app.s with the address of the start-up block */
void crt_start(const u8 *blk)
{
    char *p = (char *)blk + SB_ARGS;
    u8 i;
    in_h = blk[SB_IN];
    out_h = blk[SB_OUT];
    argc_ = blk[SB_ARGC];
    if (argc_ > 9) argc_ = 9;
    for (i = 0; i < argc_; i++) {
        argv_[i] = p;
        while (*p) p++;
        p++;
    }
    argv_[argc_] = NULL;
    exit(mk_main(argc_, argv_));
}

