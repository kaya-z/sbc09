/* SPDX-License-Identifier: MIT */
/*
 * mk09.h - the MK09 kernel interface (system calls, IPC messages, task ids)
 *
 * MK09 is a small message passing microkernel.  The kernel knows tasks,
 * synchronous messages, notifications, a timer and memory regions; everything
 * else (console, disk, file system, program loading) is done by server tasks
 * that talk to their clients with messages (see proto.h).
 *
 * The interface is written in terms of 8/16 bit integers only, so that other
 * (small) processors can implement it; the machine dependent part is the
 * system call stubs (lib/mksys.s) and arch/<cpu>/.
 */
#ifndef MK09_H
#define MK09_H

#include "types.h"

/* ---- task ids ------------------------------------------------------ */
#define TID_IDLE    0       /* the idle task (kernel)                    */
#define TID_TTY     1       /* console server                            */
#define TID_DISK    2       /* disk (block device) server                */
#define TID_FS      3       /* file system server                        */
#define TID_PM      4       /* process manager / init                    */
#define TID_USER0   5       /* first id for tasks created by PM          */
#define NTASK       8       /* number of task slots (incl. idle)         */

#define TID_ANY     0xFF    /* receive: from anybody (and notifications) */
#define TID_HW      0xFE    /* message source of a notification          */

/* ---- messages ------------------------------------------------------ */
/*
 * A message is 10 bytes.  The kernel fills in 'src' on delivery.
 * By convention 'op' is the request code (client -> server) or the status
 * (server -> client, 0 = OK, else E_*), and for bulk data requests
 * b = buffer address, c = length (a "grant", see mk_callg()).
 */
typedef struct msg {
    u8  src;
    u8  op;
    u16 a, b, c, d;
} msg_t;

#define MSG_SIZE    10

/* notification bits (mk_notify(), delivered as a message from TID_HW:
 * op = OP_NOTIFY, a = pending bits) */
#define OP_NOTIFY   0xFF
#define NFY_TICK    0x0001  /* timer tick (to the task that did mk_hook())  */
#define NFY_EXIT    0x0002  /* a task has exited (to PM)                     */
#define NFY_INT     0x0004  /* interrupt key (^C) typed on the console (PM)  */
#define NFY_USER    0x0008  /* free for user protocols                       */

/* ---- status / error codes ------------------------------------------ */
#define E_OK        0
#define E_PERM      1       /* not allowed (privileged call)              */
#define E_NOTASK    2       /* no such task                               */
#define E_DEAD      3       /* the peer task has exited                   */
#define E_FAULT     4       /* bad address / length                       */
#define E_NOSLOT    5       /* no free task slot                          */
#define E_NOMEM     6       /* memory region busy or invalid              */
#define E_NOTWAIT   7       /* reply: the client is not waiting           */
#define E_INVAL     8       /* bad argument                               */
#define E_NOENT     9       /* file not found                             */
#define E_EXIST     10      /* file exists                                */
#define E_NOTDIR    11
#define E_ISDIR     12
#define E_NOSPC     13      /* disk full                                  */
#define E_IO        14      /* device error                               */
#define E_BADF      15      /* bad file handle                            */
#define E_MFILE     16      /* too many open files                        */
#define E_NOTEMPTY  17
#define E_NAME      18      /* bad file name                              */
#define E_FORMAT    19      /* not an executable / bad file system        */
#define E_BUSY      20
#define E_RO        21
#define E_NOSYS     22      /* unknown request                            */
#define E_EOF       23

/* ---- system calls (see lib/mksys.s) --------------------------------- */
/*
 * All return an E_* status unless noted.  The system calls are made with the
 * SWI instruction: A = call number, B = u8 argument, X, Y, U = u16 arguments;
 * the result is returned in B (status) and X (value).
 */
u8  mk_send(u8 dst, msg_t *m);              /* rendezvous send (blocks)       */
u8  mk_recv(u8 from, msg_t *m);             /* receive (blocks)               */
u8  mk_call(u8 dst, msg_t *m);              /* send + wait for the reply      */
u8  mk_callg(u8 dst, msg_t *m, u8 gflags);  /* call, grants m->b..m->b+m->c   */
u8  mk_reply(u8 dst, msg_t *m);             /* answer a client (never blocks) */
u8  mk_notify(u8 dst, u16 bits);            /* asynchronous event (never blocks) */
u8  mk_copyfrom(u8 client, u16 off, void *dst, u16 n); /* read the client's grant  */
u8  mk_copyto(u8 client, u16 off, const void *src, u16 n); /* write the client's grant */
void mk_sleep(u16 ticks);                   /* sleep (ticks of 1/TICK_HZ s)   */
u16  mk_ticks(void);                        /* uptime in ticks, low 16 bits   */
void mk_yield(void);
void mk_exit(u8 code);                      /* never returns                  */
u8  mk_self(void);                          /* own task id                    */

#define GR_READ     0x01    /* the server may read the granted area  */
#define GR_WRITE    0x02    /* the server may write the granted area */

/* privileged calls (tasks started with the PRIV flag: PM, TTY) */
u8  mk_hook(u16 bits);                      /* get NFY_TICK etc. every tick   */
/* create a task (state NEW) with memory region [lo, hi), first stack pointer 'top', entry
 * point and one argument (passed in X); returns the new task id in *tid */
typedef struct tcreate {
    u16 entry, lo, hi, top, arg;
    u8  prio;                               /* 0 = most urgent .. 6 */
} tcreate_t;
u8  mk_tcreate(tcreate_t *p, u8 *tid);
u8  mk_tstart(u8 tid);
u8  mk_tkill(u8 tid, u8 code);
u8  mk_tload(u8 tid, u16 addr, const void *src, u16 n);   /* write into a NEW task's memory */
u8  mk_treap(u8 *tid, u8 *code);            /* collect an exited task          */
void mk_power(void);                        /* switch the machine off          */

/* task information (ps) */
typedef struct taskinfo {
    u8  state;          /* TS_* */
    u8  prio;
    u16 lo, hi;         /* memory region */
    u16 sp;
    u16 cpu;            /* ticks of CPU use (wraps) */
} taskinfo_t;
#define TS_FREE     0
#define TS_READY    1
#define TS_RECV     2
#define TS_SEND     3
#define TS_SLEEP    4
#define TS_ZOMBIE   5
#define TS_NEW      6
#define TS_RUN      7   /* the running task (reported by mk_tinfo) */
u8  mk_tinfo(u8 tid, taskinfo_t *ti);

#define TICK_HZ     50
#define PRIO_IDLE   7
#define PRIO_SERVER 1
#define PRIO_USER   4

#endif
