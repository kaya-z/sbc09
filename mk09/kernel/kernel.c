/* SPDX-License-Identifier: MIT */
/*
 * kernel.c - the MK09 microkernel (portable part)
 *
 * What the kernel does:  tasks (a fixed table), a priority scheduler with round robin
 * time slices, synchronous message passing (send, receive, call, reply), asynchronous
 * notifications, sleeping, the tick of the timer, memory regions of the tasks and the
 * copying of bulk data between a client and the server that serves it.
 *
 * What it does not do: console, disk, files, programs - those are servers (servers/).
 *
 * The kernel runs to completion with interrupts disabled, on its own stack.  A task is
 * suspended with its registers saved on its own stack; 'sp' in the task table points to
 * them (struct frame, see arch.h).  Machine dependent things are in arch.h / entry.s and
 * bsp.h.
 */
#include "types.h"
#include "mk09.h"
#include "sysnum.h"
#include "arch.h"
#include "bsp.h"
#include "memmap.h"

#ifndef FASTPATH
#define FASTPATH    1
#endif
#define QUANTUM     2           /* time slice in ticks */
#define MAXCOPY     1024        /* largest single mk_copyfrom/mk_copyto (interrupts are off) */
#define BLOCKED     0xFF        /* internal: the system call has not completed */
#define STACKGUARD  16

typedef struct task {
    u8  state;                  /* TS_* */
    u8  prio;
    u8  peer;                   /* TS_RECV: awaited sender; TS_SEND: destination */
    u8  call;                   /* TS_SEND: it is a call, wait for the reply afterwards */
    u8  priv;                   /* may use the privileged calls */
    u8  gfl;                    /* GR_* granted to the server it is calling */
    u8  exitcode;
    u8  pad;
    u16 nbits;                  /* pending notifications */
    u16 sp;                     /* saved stack pointer (struct frame *) */
    u16 wake;                   /* TS_SLEEP: tick to wake up */
    u16 ubuf;                   /* user address of the message to receive */
    u16 lo, hi;                 /* memory region */
    u16 gptr, glen;             /* granted area */
    u16 cpu;
    msg_t m;                    /* message of a blocked sender */
} task_t;

static task_t tasks[NTASK];
u16 cur_sp;                     /* used by entry.s */
static u8  cur;                 /* the running task */
static u8  slice;               /* ticks left of its time slice */
static u8  resched;             /* something may have changed: the scheduler has to look at all tasks */
static u16 ticks;
static u8  hook_task;           /* receives hook_bits at every tick (0 = nobody) */
static u16 hook_bits;
static u8  idle_stack[64];
static u16 sval;                /* the value result (X) of the system call being handled */

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
static u8 uok(task_t *t, u16 addr, u16 len)
{
    u16 end = addr + len;
    return end >= addr && addr >= t->lo && end <= t->hi;
}
#define UOK(t, a, n) ((u16)((a) + (n)) >= (a) && (a) >= (t)->lo && (u16)((a) + (n)) <= (t)->hi)

/* complete the blocked system call of task t */
static void wake(u8 t, u8 status, u16 value)
{
    struct frame *f = (struct frame *)tasks[t].sp;
    SC_STATUS(f, status);
    SC_VALUE(f, value);
    tasks[t].state = TS_READY;
    tasks[t].gfl = 0;
    resched = 1;
}

static void deliver(u8 dst, const msg_t *m)
{
    arch_copy((void *)tasks[dst].ubuf, m, MSG_SIZE);
    wake(dst, E_OK, 0);
}

/* ------------------------------------------------------------------ */
/* IPC                                                                 */
/* ------------------------------------------------------------------ */
#define M_SEND  0
#define M_CALL  1
#define M_REPLY 2

static u8 ipc_send(u8 me, u8 dst, u16 ubuf, u8 mode, u8 gfl)
{
    task_t *s = &tasks[me];
    task_t *d;

    if (dst >= NTASK || dst == me) return E_NOTASK;
    d = &tasks[dst];
    if (d->state == TS_FREE || d->state == TS_NEW) return E_NOTASK;
    if (d->state == TS_ZOMBIE) return E_DEAD;
    if (!uok(s, ubuf, MSG_SIZE)) return E_FAULT;
    arch_copy(&s->m, (void *)ubuf, MSG_SIZE);
    s->m.src = me;
    s->gfl = 0;
    if (mode == M_CALL && gfl) {
        if (!uok(s, s->m.b, s->m.c)) return E_FAULT;
        s->gptr = s->m.b;
        s->glen = s->m.c;
        s->gfl = gfl;
    }
    if (d->state == TS_RECV && (d->peer == TID_ANY || d->peer == me)) {
        deliver(dst, &s->m);
        if (mode == M_CALL) {                   /* now wait for the reply */
            s->state = TS_RECV;
            s->peer = dst;
            s->ubuf = ubuf;
            return BLOCKED;
        }
        return E_OK;
    }
    if (mode == M_REPLY) return E_NOTWAIT;
    s->state = TS_SEND;
    s->peer = dst;
    s->call = (mode == M_CALL);
    s->ubuf = ubuf;
    return BLOCKED;
}

static u8 ipc_recv(u8 me, u8 from, u16 ubuf)
{
    task_t *r = &tasks[me];
    task_t *s;
    u8 i;

    if (!uok(r, ubuf, MSG_SIZE)) return E_FAULT;
    if (from == TID_ANY && r->nbits) {
        msg_t m;
        m.src = TID_HW;
        m.op = OP_NOTIFY;
        m.a = r->nbits;
        m.b = m.c = m.d = 0;
        r->nbits = 0;
        arch_copy((void *)ubuf, &m, MSG_SIZE);
        return E_OK;
    }
    for (i = 0; i < NTASK; i++) {
        s = &tasks[i];
        if (s->state == TS_SEND && s->peer == me && (from == TID_ANY || from == i)) {
            arch_copy((void *)ubuf, &s->m, MSG_SIZE);
            if (s->call) {                      /* a call: it waits for the reply now */
                s->state = TS_RECV;
                s->peer = me;
                s->call = 0;
            } else {
                wake(i, E_OK, 0);
            }
            return E_OK;
        }
    }
    if (from != TID_ANY) {
        if (from >= NTASK) return E_NOTASK;
        if (tasks[from].state == TS_ZOMBIE || tasks[from].state == TS_FREE) return E_DEAD;
    }
    r->state = TS_RECV;
    r->peer = from;
    r->ubuf = ubuf;
    return BLOCKED;
}

static u8 notify(u8 dst, u16 bits)
{
    task_t *d;
    if (dst >= NTASK) return E_NOTASK;
    d = &tasks[dst];
    if (d->state == TS_FREE || d->state == TS_NEW) return E_NOTASK;
    if (d->state == TS_ZOMBIE) return E_DEAD;
    if (d->state == TS_RECV && d->peer == TID_ANY) {
        msg_t m;
        m.src = TID_HW;
        m.op = OP_NOTIFY;
        m.a = bits | d->nbits;
        m.b = m.c = m.d = 0;
        d->nbits = 0;
        deliver(dst, &m);
    } else {
        d->nbits |= bits;
    }
    return E_OK;
}

/* copy between the memory of the caller (a server) and the area granted by a client
 * that is waiting for the caller's reply */
static u8 grant_copy(u8 me, u8 client, u16 off, u16 mem, u16 n, u8 to_client)
{
    task_t *c;
    u8 need = to_client ? GR_WRITE : GR_READ;
    if (client >= NTASK) return E_NOTASK;
    c = &tasks[client];
    if (c->state != TS_RECV || c->peer != me || !(c->gfl & need)) return E_PERM;
    if (n > MAXCOPY) return E_INVAL;
    if (off > c->glen || n > c->glen - off) return E_FAULT;
    if (!uok(&tasks[me], mem, n)) return E_FAULT;
    if (to_client)
        arch_copy((void *)(c->gptr + off), (void *)mem, n);
    else
        arch_copy((void *)mem, (void *)(c->gptr + off), n);
    return E_OK;
}

/* ------------------------------------------------------------------ */
/* tasks                                                               */
/* ------------------------------------------------------------------ */
static void task_exit(u8 t, u8 code)
{
    u8 i;
    task_t *x = &tasks[t];
    x->state = TS_ZOMBIE;
    x->exitcode = code;
    x->nbits = 0;
    for (i = 0; i < NTASK; i++) {               /* everybody who waits for it gets an error */
        task_t *o = &tasks[i];
        if ((o->state == TS_SEND || o->state == TS_RECV) && o->peer == t)
            wake(i, E_DEAD, 0);
    }
    if (hook_task == t) hook_task = 0;
    if (t != TID_PM) notify(TID_PM, NFY_EXIT);
}

static u8 task_create(tcreate_t *p, u16 *tid)
{
    u8 i;
    task_t *n = 0;
    if (p->lo >= p->hi || p->top < p->lo + FRAME_SIZE || p->top > p->hi) return E_INVAL;
    if (p->entry < p->lo || p->entry >= p->hi) return E_INVAL;
    if (p->prio >= PRIO_IDLE) return E_INVAL;
    for (i = 1; i < NTASK; i++) {               /* the region must be free */
        task_t *o = &tasks[i];
        if (o->state != TS_FREE && !(p->hi <= o->lo || p->lo >= o->hi)) return E_NOMEM;
    }
    for (i = TID_USER0; i < NTASK; i++)
        if (tasks[i].state == TS_FREE) { n = &tasks[i]; break; }
    if (!n) return E_NOSLOT;
    n->state = TS_NEW;
    n->prio = p->prio;
    n->peer = 0;
    n->call = 0;
    n->priv = 0;
    n->gfl = 0;
    n->nbits = 0;
    n->cpu = 0;
    n->lo = p->lo;
    n->hi = p->hi;
    n->sp = arch_frame_init(p->top, p->entry, p->arg);
    *tid = i;
    return E_OK;
}

static void boot_task(u8 tid, u16 entry, u16 lo, u16 hi, u8 prio, u8 priv)
{
    task_t *t = &tasks[tid];
    t->state = TS_READY;
    t->prio = prio;
    t->priv = priv;
    t->lo = lo;
    t->hi = hi;
    t->sp = arch_frame_init(hi, entry, 0);
}

/* ------------------------------------------------------------------ */
/* scheduler                                                           */
/* ------------------------------------------------------------------ */
static u8 pick(void)
{
    u8 best = NTASK, bp = 255, i;
    task_t *t;
    task_t *c = &tasks[cur];

    /* round robin: the tasks after the current one first, then the ones before it and it */
    for (i = cur + 1, t = c + 1; i < NTASK; i++, t++)
        if (t->state == TS_READY && t->prio < bp) { best = i; bp = t->prio; }
    for (i = 0, t = tasks; i <= cur; i++, t++)
        if (t->state == TS_READY && t->prio < bp) { best = i; bp = t->prio; }
    if (c->state == TS_READY && slice && c->prio <= bp) return cur;
    return best;
}

u16 k_schedule(void)
{
    u8 n;
    if (FASTPATH && !resched && slice && tasks[cur].state == TS_READY)   /* nothing happened that could change the choice */
        return tasks[cur].sp;
    resched = 0;
    n = pick();
    if (n != cur || !slice) slice = QUANTUM;
    cur = n;
    return tasks[n].sp;
}

/* ------------------------------------------------------------------ */
/* traps                                                               */
/* ------------------------------------------------------------------ */
static u8 check_stack(struct frame *f)
{
    task_t *t = &tasks[cur];
    u16 sp = (u16)f;
    if (cur == 0) return 1;
    if (sp < t->lo + STACKGUARD || sp + FRAME_SIZE > t->hi) {
        task_exit(cur, 0xFD);                   /* stack out of its region */
        return 0;
    }
    return 1;
}

void k_irq(struct frame *f)
{
    u8 i;
    tasks[cur].sp = (u16)f;
    if (!bsp_timer_fired()) return;
    ticks++;
    tasks[cur].cpu++;
    if (slice && !--slice) resched = 1;
    for (i = 1; i < NTASK; i++)
        if (tasks[i].state == TS_SLEEP && (s16)(ticks - tasks[i].wake) >= 0)
            wake(i, E_OK, 0);
    if (hook_task) notify(hook_task, hook_bits);
    check_stack(f);
}

static u8 sc_tinfo(u8 me, u8 tid, u16 ubuf)
{
    taskinfo_t ti;
    task_t *t;
    if (tid >= NTASK) return E_NOTASK;
    if (!uok(&tasks[me], ubuf, sizeof(ti))) return E_FAULT;
    t = &tasks[tid];
    ti.state = (tid == me) ? TS_RUN : t->state;
    ti.prio = t->prio;
    ti.lo = t->lo;
    ti.hi = t->hi;
    ti.sp = t->sp;
    ti.cpu = t->cpu;
    arch_copy((void *)ubuf, &ti, sizeof(ti));
    return E_OK;
}

static u8 sc_priv(u8 me, struct frame *f)
{
    u8 nr = SC_NR(f);
    task_t *t = &tasks[me];
    tcreate_t c;
    u8 tid;
    u16 tidv;
    u8 r = E_OK;

    if (!t->priv) return E_PERM;
    switch (nr) {
    case SYS_HOOK:
        hook_task = me;
        hook_bits = SC_X(f);
        break;
    case SYS_TCREATE:
        if (!uok(t, SC_X(f), sizeof(tcreate_t))) return E_FAULT;
        arch_copy(&c, (void *)SC_X(f), sizeof(tcreate_t));
        r = task_create(&c, &tidv);
        sval = tidv;
        break;
    case SYS_TSTART:
        tid = SC_B(f);
        if (tid < TID_USER0 || tid >= NTASK || tasks[tid].state != TS_NEW) return E_NOTASK;
        tasks[tid].state = TS_READY;
        resched = 1;
        break;
    case SYS_TKILL:
        tid = SC_B(f);
        if (tid < TID_USER0 || tid >= NTASK) return E_NOTASK;
        if (tasks[tid].state == TS_FREE || tasks[tid].state == TS_ZOMBIE) return E_NOTASK;
        task_exit(tid, (u8)SC_X(f));
        break;
    case SYS_TLOAD: {                           /* B = tid, X = address, Y = source, U = length */
        task_t *d;
        tid = SC_B(f);
        if (tid < TID_USER0 || tid >= NTASK) return E_NOTASK;
        d = &tasks[tid];
        if (d->state != TS_NEW) return E_NOTASK;
        if (!uok(d, SC_X(f), SC_U(f)) || !uok(t, SC_Y(f), SC_U(f))) return E_FAULT;
        arch_copy((void *)SC_X(f), (void *)SC_Y(f), SC_U(f));
        break;
    }
    case SYS_TREAP:
        for (tid = TID_USER0; tid < NTASK; tid++)
            if (tasks[tid].state == TS_ZOMBIE) {
                sval = ((u16)tid << 8) | tasks[tid].exitcode;
                tasks[tid].state = TS_FREE;
                tasks[tid].lo = tasks[tid].hi = 0;
                return E_OK;
            }
        return E_NOTASK;
    case SYS_POWER:
        bsp_power_off();
        break;
    default:
        r = E_NOSYS;
    }
    return r;
}

void k_swi(struct frame *f)
{
    u8 me = cur;
    task_t *t = &tasks[me];
    u8 r = E_NOSYS;

    t->sp = (u16)f;
    if (!check_stack(f)) return;
    sval = 0;
    switch (SC_NR(f)) {
    case SYS_SEND:    r = ipc_send(me, SC_B(f), SC_X(f), M_SEND, 0); break;
    case SYS_CALL:    r = ipc_send(me, SC_B(f), SC_X(f), M_CALL, 0); break;
    case SYS_CALLG:   r = ipc_send(me, SC_B(f), SC_X(f), M_CALL, (u8)SC_Y(f)); break;
    case SYS_REPLY:   r = ipc_send(me, SC_B(f), SC_X(f), M_REPLY, 0); break;
    case SYS_RECV:    r = ipc_recv(me, SC_B(f), SC_X(f)); break;
    case SYS_NOTIFY:  r = notify(SC_B(f), SC_X(f)); break;
    case SYS_COPYFROM: r = grant_copy(me, SC_B(f), SC_X(f), SC_Y(f), SC_U(f), 0); break;
    case SYS_COPYTO:  r = grant_copy(me, SC_B(f), SC_X(f), SC_Y(f), SC_U(f), 1); break;
    case SYS_SLEEP:
        if (SC_X(f)) {
            t->wake = ticks + SC_X(f);
            t->state = TS_SLEEP;
            r = BLOCKED;
        } else {
            slice = 0;
            resched = 1;
            r = E_OK;
        }
        break;
    case SYS_TICKS:   sval = ticks; r = E_OK; break;
    case SYS_YIELD:   slice = 0; resched = 1; r = E_OK; break;
    case SYS_SELF:    sval = me; r = E_OK; break;
    case SYS_EXIT:    task_exit(me, SC_B(f)); r = BLOCKED; break;
    case SYS_TINFO:   r = sc_tinfo(me, SC_B(f), SC_X(f)); break;
    default:          r = sc_priv(me, f); break;
    }
    if (r != BLOCKED) {
        SC_STATUS(f, r);
        SC_VALUE(f, sval);
    }
}

/* ------------------------------------------------------------------ */
/* start                                                               */
/* ------------------------------------------------------------------ */
u16 kmain(void)
{
    task_t *i = &tasks[0];
    i->state = TS_READY;
    i->prio = PRIO_IDLE;
    i->lo = (u16)idle_stack;
    i->hi = (u16)idle_stack + sizeof(idle_stack);
    i->sp = arch_frame_init(i->hi, (u16)arch_idle_loop, 0);

    boot_task(TID_TTY,  TTY_CODE,  TTY_DATA,  TTY_DATA + TTY_DSZ,   PRIO_SERVER, 1);
    boot_task(TID_DISK, DISK_CODE, DISK_DATA, DISK_DATA + DISK_DSZ, PRIO_SERVER, 0);
    boot_task(TID_FS,   FS_CODE,   FS_DATA,   FS_DATA + FS_DSZ,     PRIO_SERVER, 0);
    boot_task(TID_PM,   PM_CODE,   PM_DATA,   PM_DATA + PM_DSZ,     PRIO_SERVER, 1);

    bsp_timer_start();
    cur = 0;
    slice = 0;
    return k_schedule();
}
