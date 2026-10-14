/* SPDX-License-Identifier: MIT */
/* arch.c - MC6809: the first frame of a new task */
#include "arch.h"

u16 arch_frame_init(u16 top, u16 entry, u16 arg)
{
    struct frame *f = (struct frame *)(top - FRAME_SIZE);
    f->cc = CC_INIT;
    f->a = 0;
    f->b = 0;
    f->dp = 0;
    f->x = arg;
    f->y = 0;
    f->u = 0;
    f->pc = entry;
    return (u16)f;
}
