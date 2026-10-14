/* SPDX-License-Identifier: MIT */
/*
 * tty.c - the console server (TASK 1)
 *
 * Owns the ACIA.  Reads are served from a ring buffer that is filled by polling the ACIA at
 * every timer tick (the ACIA has no interrupt) and whenever a request arrives.
 *
 *   cooked mode (default)  a line editor: echo, BS/DEL erase one character (multi-byte
 *                          characters of UTF-8 / EUC-JP / Shift_JIS as a whole, with the right
 *                          number of columns), ^U kills the line, ^D at the start of a line
 *                          is end of file, ^C (with TM_ISIG) tells PM to stop the foreground
 *                          program, cursor key sequences are swallowed.  A read returns one line.
 *   raw mode               no editing, no echo (unless TM_ECHO): a read returns what has arrived.
 *
 * The terminal (or the HDMI screen of the FPGA board) does the display of the characters;
 * the server only needs the character set to edit lines.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "bsp.h"
#include "mkstr.h"

#define INQ         128             /* input ring (power of two) */
#define LINEMAX     120
#define CHUNK       48

static u8 inq[INQ];
static u8 qh, qt;                   /* ring head (write) and tail (read) */
static u8 mode = TM_DEFAULT;
static u8 charset = CS_UTF8;
static u8 cols = 80, rows = 24;

static u8 line[LINEMAX];            /* the line being edited / read */
static u8 llen, lpos;
static u8 lready;                   /* the line is complete (llen may be 0: end of file) */
static u8 esc;                      /* swallowing an escape sequence: 1 after ESC, 2 inside */
static u8 lastcr;

static u8 rd_tid;                   /* client waiting in TTY_READ (0 = none) */
static u16 rd_len;

/* ------------------------------------------------------------------ */
static void out(u8 c)
{
    while (!(ACIA_STATUS & ACIA_TDRE))
        ;
    ACIA_DATA = c;
}

static void outc(u8 c)
{
    if (c == '\n' && (mode & TM_CRLF)) out('\r');
    out(c);
}

static void reply(u8 tid, u8 st, u16 a)
{
    msg_t r;
    r.op = st;
    r.a = a;
    r.b = r.c = r.d = 0;
    mk_reply(tid, &r);
}

static u8 qcount(void)
{
    return (u8)(qh - qt) & (INQ - 1);
}

static void poll_acia(void)
{
    while ((ACIA_STATUS & ACIA_RDRF) && (u8)((qh + 1) & (INQ - 1)) != qt) {
        inq[qh] = ACIA_DATA;
        qh = (u8)((qh + 1) & (INQ - 1));
    }
}

static u8 deq(void)
{
    u8 c = inq[qt];
    qt = (u8)((qt + 1) & (INQ - 1));
    return c;
}

/* ------------------------------------------------------------------ */
/* characters                                                          */
/* ------------------------------------------------------------------ */
/* bytes of the character that starts with c */
static u8 clen(u8 c)
{
    switch (charset) {
    case CS_UTF8:
        if (c >= 0xF0) return 4;
        if (c >= 0xE0) return 3;
        if (c >= 0xC2) return 2;
        return 1;
    case CS_EUC:
        if (c == 0x8F) return 3;
        if (c == 0x8E || (c >= 0xA1 && c <= 0xFE)) return 2;
        return 1;
    case CS_SJIS:
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) return 2;
        return 1;
    }
    return 1;
}

/* columns of the character of n bytes at p */
static u8 cwidth(const u8 *p, u8 n)
{
    switch (charset) {
    case CS_UTF8:
        if (n == 4) return 2;
        if (n == 3) {
            if (p[0] >= 0xE3 && p[0] <= 0xED) return 2;     /* CJK, kana, hangul */
            if (p[0] == 0xEF) {
                if (p[1] == 0xBD && p[2] >= 0xA1) return 1;  /* halfwidth katakana */
                if (p[1] == 0xBE && p[2] <= 0x9F) return 1;
                if (p[1] == 0xBC || p[1] == 0xBD) return 2;  /* fullwidth forms */
                if (p[1] == 0xBF && p[2] >= 0xA0 && p[2] <= 0xA6) return 2;
            }
        }
        return 1;
    case CS_EUC:
        if (n == 3) return 2;
        if (n == 2) return p[0] == 0x8E ? 1 : 2;
        return 1;
    case CS_SJIS:
        return n == 2 ? 2 : 1;
    }
    return 1;
}

static void erase_last(void)
{
    u8 i = 0, st = 0, w, n;
    if (!llen) return;
    while (i < llen) {                  /* find where the last character starts */
        st = i;
        n = clen(line[i]);
        if (n > llen - i) n = llen - i;
        i += n;
    }
    w = cwidth(line + st, llen - st);
    llen = st;
    if (mode & TM_ECHO) {
        for (i = 0; i < w; i++) out(8);
        for (i = 0; i < w; i++) out(' ');
        for (i = 0; i < w; i++) out(8);
    }
}

static void edit(u8 c)
{
    if (esc) {                          /* swallow ESC [ ... final / ESC O x */
        if (esc == 1) esc = (c == '[' || c == 'O') ? 2 : 0;
        else if (c >= 0x40 && c <= 0x7E) esc = 0;
        return;
    }
    if (c == '\n' && lastcr) { lastcr = 0; return; }
    lastcr = (c == '\r');
    switch (c) {
    case 0x1B:
        esc = 1;
        return;
    case '\r':
    case '\n':
        if (mode & TM_ECHO) outc('\n');
        line[llen++] = '\n';
        lpos = 0;
        lready = 1;
        return;
    case 0x08:
    case 0x7F:
        erase_last();
        return;
    case 0x15:                          /* ^U */
        while (llen) erase_last();
        return;
    case 0x03:                          /* ^C */
        if (mode & TM_ISIG) {
            if (mode & TM_ECHO) {
                out('^');
                out('C');
                outc('\n');
            }
            llen = lpos = 0;
            if (rd_tid) {                       /* the reader gets an empty line */
                line[0] = '\n';
                llen = 1;
                lready = 1;
            }
            mk_notify(TID_PM, NFY_INT);
            return;
        }
        break;
    case 0x04:                          /* ^D at the start of a line: end of file */
        if (!llen) {
            lpos = 0;
            lready = 1;
        }
        return;
    }
    if (c < 0x20) return;
    if (llen < LINEMAX - 2) {
        line[llen++] = c;
        if (mode & TM_ECHO) out(c);
    }
}

static void reset_line(void)
{
    llen = lpos = lready = esc = lastcr = 0;
}

/* run the line editor on what has arrived */
static void pump(void)
{
    poll_acia();
    if (mode & TM_RAW) return;
    while (!lready && qcount())
        edit(deq());
}

/* ------------------------------------------------------------------ */
/* requests                                                            */
/* ------------------------------------------------------------------ */
static void try_reader(void)
{
    u16 n, done;
    u8 tmp[CHUNK];
    u8 c, i;

    if (!rd_tid) return;
    pump();
    if (mode & TM_RAW) {
        n = qcount();
        if (!n) {
            if (mode & TM_NOWAIT) { reply(rd_tid, E_OK, 0); rd_tid = 0; }
            return;
        }
        if (n > rd_len) n = rd_len;
        done = 0;
        while (done < n) {
            c = (u8)((n - done) > CHUNK ? (u16)CHUNK : (n - done));
            for (i = 0; i < c; i++) tmp[i] = deq();
            if (mk_copyto(rd_tid, done, tmp, c)) break;
            done += c;
        }
        reply(rd_tid, E_OK, done);
        rd_tid = 0;
        return;
    }
    if (!lready) {
        if (mode & TM_NOWAIT) { reply(rd_tid, E_OK, 0); rd_tid = 0; }
        return;
    }
    n = llen - lpos;
    if (n > rd_len) n = rd_len;
    if (n && mk_copyto(rd_tid, 0, line + lpos, n)) n = 0;
    lpos += (u8)n;
    if (lpos >= llen) reset_line();
    reply(rd_tid, E_OK, n);
    rd_tid = 0;
}

static void do_write(msg_t *m)
{
    u16 n = m->c, off = 0;
    u8 buf[CHUNK];
    u8 c, i, st = E_OK;

    while (off < n) {
        c = (u8)((n - off) > CHUNK ? (u16)CHUNK : (n - off));
        st = mk_copyfrom(m->src, off, buf, c);
        if (st) break;
        for (i = 0; i < c; i++) outc(buf[i]);
        off += c;
        poll_acia();
    }
    reply(m->src, off ? E_OK : st, off);
}

static void do_ioctl(msg_t *m)
{
    msg_t r;
    u8 oldraw = mode & TM_RAW;

    r.op = E_OK;
    r.a = r.b = r.c = r.d = 0;
    switch (m->a) {
    case TTYC_GETMODE:
        r.a = mode;
        r.b = charset;
        r.c = cols;
        r.d = rows;
        break;
    case TTYC_SETMODE:
        mode = (u8)m->b;
        if (m->c != 0xFFFF && m->c <= CS_SJIS) charset = (u8)m->c;
        if (m->d != 0xFFFF) { cols = (u8)(m->d >> 8); rows = (u8)m->d; }
        if (oldraw != (mode & TM_RAW)) reset_line();
        break;
    case TTYC_READY:
        pump();
        r.a = (mode & TM_RAW) ? (u16)qcount() : (lready ? (u16)(llen - lpos) : (u16)0);
        break;
    case TTYC_FLUSH:
        qh = qt = 0;
        reset_line();
        break;
    default:
        r.op = E_INVAL;
    }
    mk_reply(m->src, &r);
}

int main(void)
{
    msg_t m;
    taskinfo_t ti;

    mk_hook(NFY_TICK);
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) {
            pump();
            try_reader();
            continue;
        }
        switch (m.op) {
        case TTY_WRITE:
            do_write(&m);
            break;
        case TTY_READ:
            if (rd_tid && rd_tid != m.src) {        /* is the previous reader still waiting? */
                if (!mk_tinfo(rd_tid, &ti) && ti.state == TS_RECV) {
                    reply(m.src, E_BUSY, 0);
                    break;
                }
            }
            rd_tid = m.src;
            rd_len = m.c;
            try_reader();
            break;
        case TTY_IOCTL:
            do_ioctl(&m);
            break;
        default:
            reply(m.src, E_NOSYS, 0);
        }
    }
    return 0;
}
