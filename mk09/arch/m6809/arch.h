/* SPDX-License-Identifier: MIT */
/*
 * arch.h - the machine dependent interface of the MK09 kernel (MC6809)
 *
 * What the portable kernel (kernel/kernel.c) needs from a CPU:
 *   - struct frame: the register state saved on a task's stack at a trap
 *   - the system call register convention (SC_* accessors)
 *   - arch_frame_init(): make a new task's first frame
 *   - arch_copy(): copy memory between (the visible) address spaces
 *   - arch_uok(): is a user range acceptable (here: inside the task's region)
 * and in assembler (entry.s): the trap entries, the switch to the kernel stack, the return
 * to a task and the idle loop.
 */
#ifndef ARCH_H
#define ARCH_H

#include "types.h"

/* the 6809 stacks CC A B DP X Y U PC (entire state, E flag set) at SWI and IRQ */
struct frame {
    u8  cc, a, b, dp;
    u16 x, y, u, pc;
};
#define FRAME_SIZE      12

/* system call convention: A = number, B = u8 argument, X, Y, U = arguments.
 * Result: B = status, X = value. */
#define SC_NR(f)        ((f)->a)
#define SC_B(f)         ((f)->b)
#define SC_X(f)         ((f)->x)
#define SC_Y(f)         ((f)->y)
#define SC_U(f)         ((f)->u)
#define SC_STATUS(f, v) ((f)->b = (u8)(v))
#define SC_VALUE(f, v)  ((f)->x = (u16)(v))

#define CC_INIT         0x80        /* E flag set, interrupts enabled */

/* build the first frame of a task at the top of its stack area; returns the saved sp */
u16  arch_frame_init(u16 stack_top, u16 entry, u16 arg);

void arch_copy(void *dst, const void *src, u16 n);

/* assembler */
extern u16 cur_sp;                  /* sp of the interrupted task (set by the trap entry) */
void arch_idle_loop(void);          /* the code of the idle task */
extern u8  kstack[];                /* kernel stack */

#endif
