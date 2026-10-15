/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * life - Conway's Game of Life on the console screen (ANSI cursor addressing: the HDMI screen of the
 * board, or a terminal)
 *
 *   life [-s seed] [-d ticks] [-n generations]
 *
 * The field is as big as the screen (the last line shows the status) and its edges are joined
 * (a torus).  Only the cells that change are drawn.  Keys:  q quit   r new random field
^ * space pause / go on   + faster   - slower   ^C quit.  -d is the pause between the generations in ticks of 20 ms
 * (default 2), -n stops after that many generations (the screen stays as it is).
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define MAXW 100
#define MAXH 40
#define STRIDE (MAXW + 2)
#define OBUF 96

static u8 g[2][STRIDE * (MAXH + 2)];
static u8 colsum[MAXW + 2];
static u8 W, H;
static u8 *cg, *ng;        /* the current and the next generation (pointers: CMOC computes u8 * constant in 8 bits) */
static u16 row(u8 y) { return (u16)y * STRIDE; }
static u16 gen, alive, seed;
static u8 ob[OBUF];
static u8 on;
static u8 ly, lx;           /* the cell after the last drawn one (the cursor is there) */

static void flush(void)
{
    if (on) write(1, ob, on);
    on = 0;
}

static void put(u8 c)
{
    if (on == OBUF) flush();
    ob[on++] = c;
}

static void putn(u16 v)
{
    char s[6], *q = s;
    utoa_(v, s, 10);
    while (*q) put((u8)*q++);
}

static void puts_(const char *s)
{
    while (*s) put((u8)*s++);
}

static void gotoxy(u8 y, u8 x)
{
    put(27);
    put('[');
    putn(y);
    put(';');
    putn(x);
    put('H');
}

static u8 blank[MAXW + 2];  /* an empty row: the state of the screen after the clear */
static u8 *rowp;            /* the state of the screen in the row that is being drawn */

/* draw cell x of row y; a short gap in the same row is filled with the cells that are there
 * (cheaper than moving the cursor: ESC [ y ; x H is 6 to 8 bytes) */
static void draw(u8 y, u8 x, u8 on_)
{
    if (y == ly && x > lx && (u8)(x - lx) <= 5) {
        while (lx < x) {
            put(rowp[lx] ? '#' : ' ');
            lx++;
        }
    } else if (y != ly || x != lx) gotoxy(y, x);
    put(on_ ? '#' : ' ');
    ly = y;
    lx = (u8)(x + 1);
}

static u8 rnd(void)               /* xorshift (7, 9, 8) on 16 bits; returns the high byte */
{
    u16 x = seed;
    x = x ^ (u16)(x << 7);
    x = x ^ (u16)(x >> 9);
    x = x ^ (u16)(x << 8);
    seed = x;
    return (u8)(x >> 8);
}

static void status(u8 paused, u8 delay)
{
    gotoxy((u8)(H + 1), 1);
    puts_("gen ");
    putn(gen);
    puts_("  alive ");
    putn(alive);
    puts_("  speed ");
    putn(delay);
    puts_(paused ? "  PAUSED " : "  q r space +/-   ");
    put(27); put('['); put('K');          /* clear to the end of the line */
    ly = 0;
}

static void clear_screen(void)
{
    put(27); put('['); put('2'); put('J');
    put(27); put('['); put('H');
    ly = 0;
}

static void randomize(void)
{
    u8 y, x;
    memset(cg, 0, sizeof(g[0]));
    alive = 0;
    for (y = 1; y <= H; y++)
        for (x = 1; x <= W; x++)
            if (rnd() < 64) { cg[row(y) + x] = 1; alive++; }
    gen = 0;
    clear_screen();
    rowp = blank;
    for (y = 1; y <= H; y++)
        for (x = 1; x <= W; x++)
            if (cg[row(y) + x]) draw(y, x, 1);
}

static void step(void)
{
    u8 *a, *b, *c, *n, *o, *t;
    u8 y, x, s, v;
    u16 cnt = 0;
    o = cg;
    n = ng;
    /* the edges are joined: copy the opposite sides into the border */
    for (y = 1; y <= H; y++) {
        t = o + row(y);
        t[0] = t[W];
        t[W + 1] = t[1];
    }
    memcpy(o, o + row(H), W + 2);
    memcpy(o + row((u8)(H + 1)), o + row(1), W + 2);
    for (y = 1; y <= H; y++) {
        u8 *pa, *pb, *pc, *cs, *bb, *tt;
        a = o + row((u8)(y - 1));
        b = a + STRIDE;
        c = b + STRIDE;
        pa = a; pb = b; pc = c; cs = colsum;
        for (x = W + 2; x; x--)
            *cs++ = (u8)(*pa++ + *pb++ + *pc++);
        cs = colsum;
        bb = b + 1;
        tt = n + row(y) + 1;
        rowp = b;
        for (x = 1; x <= W; x++) {
            s = (u8)(cs[0] + cs[1] + cs[2]);               /* the cell itself is in it */
            v = (u8)((s == 3) | ((s == 4) & *bb));
            *tt++ = v;
            cnt += v;
            if (v != *bb) draw(y, x, v);
            cs++;
            bb++;
        }
    }
    cg = n;
    ng = o;
    alive = cnt;
    gen++;
}

int main(int argc, char **argv)
{
    u8 mode, cs, cols, rows, paused = 0;
    u16 delay = 2, maxgen = 0;
    int i, k;
    seed = mk_ticks() | 0x1000;
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-s")) seed = (u16)atoi_(argv[i + 1]);
        else if (!strcmp(argv[i], "-d")) delay = (u16)atoi_(argv[i + 1]);
        else if (!strcmp(argv[i], "-n")) maxgen = (u16)atoi_(argv[i + 1]);
        else { puts("usage: life [-s seed] [-d ticks] [-n generations]"); return 1; }
    }
    tty_getmode(&mode, &cs, &cols, &rows);
    W = cols ? cols : 80;
    H = (u8)((rows ? rows : 24) - 1);
    if (W > MAXW) W = MAXW;
    if (H > MAXH) H = MAXH;
    cg = g[0];
    ng = g[1];
    tty_setmode((mode | TM_RAW) & ~TM_ECHO, 0xFF, 0xFF, 0xFF);
    randomize();
    status(0, (u8)delay);
    flush();
    for (;;) {
        if (tty_ready() > 0) {
            k = getchar();
            if (k == 'q' || k == 'Q' || k == 3) break;
            if (k == 'r' || k == 'R') { randomize(); status(paused, (u8)delay); }
            else if (k == ' ') { paused ^= 1; status(paused, (u8)delay); }
            else if (k == '+' && delay > 0) { delay--; status(paused, (u8)delay); }
            else if (k == '-' && delay < 50) { delay++; status(paused, (u8)delay); }
            flush();
        }
        if (paused || (maxgen && gen >= maxgen)) {
            flush();
            if (maxgen && gen >= maxgen) break;
            mk_sleep(5);
            continue;
        }
        step();
        if ((gen & 3) == 0 || (maxgen && gen >= maxgen)) status(paused, (u8)delay);
        flush();
        if (delay) mk_sleep(delay);
    }
    gotoxy((u8)(H + 1), 1);
    put('\n');
    flush();
    tty_setmode(mode, 0xFF, 0xFF, 0xFF);
    return 0;
}
