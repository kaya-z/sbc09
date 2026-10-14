/* SPDX-License-Identifier: MIT */
/*
 * pm.c - the process manager (TASK 4)
 *
 * Starts programs (MX files from the file system) and waits for them: the shell asks for
 * PM_SPAWN and gets the answer when the program has exited.  PM also
 *   - starts the shell when the system comes up and again when it has gone,
 *   - takes the shell out of memory when a big program needs its space (the shell sits at
 *     the top of the transient area, like the command processor of CP/M) and loads it again
 *     when that program is done,
 *   - closes the files of exited programs (FS_CLOSEALL) and stops the foreground program on ^C,
 *   - tells the time (PM_TIME) and powers the machine off (PM_SHUTDOWN).
 *
 * The program file starts with a 16 byte header (lib/crt0app.s).  It is loaded at its load
 * address, followed by the bss, the stack and the start-up block (mklib.h) with the arguments.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "bsp.h"
#include "mklib.h"
#include "mkstr.h"
#include "memmap.h"

#define HDRSIZE     16
#define IOBUF       256
#define NOH         0xFF

static u8 iobuf[IOBUF];
static u8 sb[ARGMAX];                   /* start-up block of the program being started */
static u8 waiter[NTASK];                /* task to answer when the program exits */
static u8 shell_tid;                    /* the shell (0 = not running) */
static u8 fg;                           /* foreground program (0 = none) */
static u8 nproc;                        /* programs running (not counting the shell) */
static u8 respawn;                      /* start the shell again when everything is done */

static u8 fs_call(u8 op, u16 a, u16 b, u16 c, u8 gfl, msg_t *m)
{
    u8 st;
    m->op = op;
    m->a = a;
    m->b = b;
    m->c = c;
    st = gfl ? mk_callg(TID_FS, m, gfl) : mk_call(TID_FS, m);
    if (st) return st;
    return m->op;
}

static u16 be16(const u8 *p)
{
    return ((u16)p[0] << 8) | p[1];
}

static void say(const char *s)         /* (a grant must be in the data of this task, not in its code) */
{
    msg_t m;
    strcpy((char *)iobuf, s);
    m.op = TTY_WRITE;
    m.b = (u16)iobuf;
    m.c = (u16)strlen(s);
    mk_callg(TID_TTY, &m, GR_READ);
}

static void reply(u8 tid, u8 st, u16 a)
{
    msg_t r;
    r.op = st;
    r.a = a;
    r.b = r.c = r.d = 0;
    mk_reply(tid, &r);
}

static void collect(void);

/* ------------------------------------------------------------------ */
/* start a program; the arguments are in sb[]; 'who' answers when it exits (0: nobody) */
static u8 start_program(u8 who, u8 is_shell, u8 *tidp)
{
    msg_t m;
    tcreate_t tc;
    u16 size, end, n, off, load;
    u16 bss, stk;
    u8 st, h, tid = 0, created = 0;

    st = fs_call(FS_OPEN, FO_READ, (u16)(sb + SB_ARGS), (u16)strlen((char *)sb + SB_ARGS) + 1, GR_READ, &m);
    if (st) return st;
    h = (u8)m.a;
    st = fs_call(FS_FSTAT, h, 0, 0, 0, &m);
    size = m.b;
    if (!st && m.c) st = E_FORMAT;
    if (!st) st = fs_call(FS_READ, h, (u16)iobuf, IOBUF, GR_WRITE, &m);
    if (st) goto out;
    n = m.a;
    if (n < HDRSIZE || iobuf[0] != 'M' || iobuf[1] != 'K' || iobuf[2] != 1) { st = E_FORMAT; goto out; }
    load = be16(iobuf + 4);
    bss = be16(iobuf + 10);
    stk = be16(iobuf + 12);
    end = load + size + bss + stk + ARGMAX;
    if (end < load || load < TPA_BASE || end > TPA_END || size < HDRSIZE) { st = E_NOMEM; goto out; }

    if (shell_tid && load < SH_END && end > SH_BASE) {          /* it needs the shell's place */
        if (who != shell_tid) { st = E_NOMEM; goto out; }
        mk_tkill(shell_tid, 0);
        collect();
        respawn = 1;
        who = 0;
    }
    tc.entry = be16(iobuf + 6);
    tc.lo = load;
    tc.hi = end;
    tc.top = end - ARGMAX;
    tc.arg = end - ARGMAX;
    tc.prio = PRIO_USER;
    st = mk_tcreate(&tc, &tid);
    if (st) goto out;
    created = 1;
    off = 0;
    while (n) {
        st = mk_tload(tid, load + off, iobuf, n);
        if (st) goto out;
        off += n;
        if (off >= size) break;
        st = fs_call(FS_READ, h, (u16)iobuf, IOBUF, GR_WRITE, &m);
        if (st) goto out;
        n = m.a;
        if (off + n > size) n = size - off;
    }
    st = mk_tload(tid, end - ARGMAX, sb, ARGMAX);
    if (st) goto out;
    if (sb[SB_IN] != NOH) fs_call(FS_CHOWN, sb[SB_IN], tid, 0, 0, &m);
    if (sb[SB_OUT] != NOH) fs_call(FS_CHOWN, sb[SB_OUT], tid, 0, 0, &m);
    st = mk_tstart(tid);
out:
    fs_call(FS_CLOSE, h, 0, 0, 0, &m);
    if (st) {
        if (created) mk_tkill(tid, 255);
        return st;
    }
    waiter[tid] = who;
    if (is_shell) shell_tid = tid;
    else { nproc++; fg = tid; }
    *tidp = tid;
    return E_OK;
}

static void start_shell(void)
{
    u8 tid;
    u8 st;
    msg_t m;
    memset(sb, 0, ARGMAX);
    sb[0] = SB_VERSION;
    sb[SB_IN] = sb[SB_OUT] = NOH;
    sb[SB_ARGC] = 1;
    strcpy((char *)sb + SB_ARGS, "/BIN/SH.MX");
    if (respawn) {
        sb[SB_ARGC] = 2;
        strcpy((char *)sb + SB_ARGS + 11, "-r");
    }
    st = start_program(0, 1, &tid);
    if (st) {
        say("\r\nMK09: cannot start /BIN/SH.MX (error ");
        iobuf[0] = (u8)('0' + st / 10);
        iobuf[1] = (u8)('0' + st % 10);
        iobuf[2] = ')';
        iobuf[3] = '\r';
        iobuf[4] = '\n';
        m.op = TTY_WRITE;
        m.b = (u16)iobuf;
        m.c = 5;
        mk_callg(TID_TTY, &m, GR_READ);
        mk_sleep(250);
    } else {
        respawn = 0;
    }
}

/* handle everything that has exited */
static void collect(void)
{
    msg_t m;
    u8 t, c, w;
    while (!mk_treap(&t, &c)) {
        fs_call(FS_CLOSEALL, t, 0, 0, 0, &m);
        w = waiter[t];
        waiter[t] = 0;
        if (t == shell_tid) {
            shell_tid = 0;
        } else {
            if (nproc) nproc--;
            if (fg == t) fg = 0;
            if (w) reply(w, E_OK, c);
        }
    }
}

static void reap(void)
{
    collect();
    if (!shell_tid && !nproc) start_shell();
}

static void do_spawn(msg_t *m)
{
    u8 tid, st, i;
    u16 n = m->c;

    if (n < 2 || n > ARGMAX - SB_ARGS - 1) { reply(m->src, E_INVAL, 0); return; }
    memset(sb, 0, ARGMAX);
    sb[0] = SB_VERSION;
    sb[SB_IN] = (m->a == NOHANDLE) ? NOH : (u8)m->a;
    sb[SB_OUT] = (m->d == NOHANDLE) ? NOH : (u8)m->d;
    if (mk_copyfrom(m->src, 0, sb + SB_ARGS, n)) { reply(m->src, E_FAULT, 0); return; }
    sb[SB_ARGS + n] = 0;
    sb[SB_ARGS + n - 1] = 0;
    for (i = SB_ARGS; i < SB_ARGS + n; i++)                     /* count the arguments */
        if (sb[i] == 0 && sb[i + 1] != 0) sb[SB_ARGC]++;
    sb[SB_ARGC]++;
    st = start_program(m->src, 0, &tid);
    if (st) reply(m->src, st, 0);
}

int main(void)
{
    msg_t m;
    u16 d, t;

    say("MK09: kernel  TTY DISK FS PM started\r\n");
    d = fs_call(FS_SYNC, 0, 0, 0, 0, &m);             /* the first FS call mounts the volume */
    say(d ? "MK09: no volume (FAT16) on the card\r\n" : "MK09: disk    SD card ready\r\nMK09: fs      FAT16 mounted\r\n");
    say("MK09: loading /BIN/SH.MX\r\n");
    start_shell();
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) {
            if (m.a & NFY_EXIT) reap();
            if ((m.a & NFY_INT) && fg) mk_tkill(fg, 130);
            continue;
        }
        switch (m.op) {
        case PM_SPAWN:
            do_spawn(&m);
            break;
        case PM_TIME:
            bsp_now_fat(&d, &t);
            m.op = E_OK;
            m.a = d;
            m.b = t;
            mk_reply(m.src, &m);
            break;
        case PM_SHUTDOWN:
            fs_call(FS_SYNC, 0, 0, 0, 0, &m);
            mk_power();
            break;
        default:
            reply(m.src, E_NOSYS, 0);
        }
    }
    return 0;
}
