/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * basic - a small BASIC interpreter for MK09, in the manner of the Microsoft BASICs of the 1980s
 *
 *   basic [program.bas]       without a file: the interactive mode; with a file: load it, run it, leave
 *
 * Numbers are 32 bit floating point (sfp.h, software), strings are up to 63 bytes.
 *   Commands : NEW LIST [a[-b]] RUN [n] LOAD "f" SAVE "f" BYE (QUIT, SYSTEM)
 *   Statements: REM ' LET PRINT ? INPUT IF..THEN..ELSE GOTO GOSUB RETURN FOR..TO..STEP NEXT END STOP
 *             DIM CLS ON..GOTO/GOSUB DATA READ RESTORE RANDOMIZE
 *   Operators : + - * / ^ = <> < > <= >= AND OR NOT, strings: + and comparison
 *   Functions : ABS INT SGN SQR SIN COS TAN ATN EXP LOG RND LEN ASC VAL CHR$ STR$ LEFT$ RIGHT$ MID$ SPC TAB
 * Variables: a letter and one more letter or digit are significant (A, AB, A1), A$ for strings
 * (one letter only), arrays of numbers with one or two dimensions (DIM A(10), B(3,4); 11 elements by default).
 * ^C stops a running program (the console is in line mode, the shell stops the program).
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#define SFP_ASM
#include "sfpm.h"

#define PGM     5300
#define NVARS   (26 * 37)
#define APOOL   1000
#define STRL    48
#define LINEMAX 200
#define FORMAX  8
#define GOSMAX  16
#define OBUF    128

/* ---- output */
static u8 ob[OBUF];
static u8 on, col;

static void flush(void)
{
    if (on) write(1, ob, on);
    on = 0;
}

static void outc(char c)
{
    if (on == OBUF) flush();
    ob[on++] = (u8)c;
    if (c == '\n') col = 0; else col++;
}

static void outs(const char *s)
{
    while (*s) outc(*s++);
}

static void outn(u16 v)
{
    char b[8];
    utoa_(v, b, 10);
    outs(b);
}

/* ---- program and variables */
static u8 pool[PGM];                 /* lines: [u16 number][u8 len incl NUL][text] */
static u8 *pend;                     /* end of the program */
static f32 numv[NVARS];
static char strv[26][STRL];
static f32 apool[APOOL];
static u16 aused;
typedef struct { u16 off; u8 d1, d2; u8 dim; } adesc;
static adesc arr[26];

static char dbuf[LINEMAX + 4];       /* the line of the direct mode: same layout as a program line */
static u8 *cl;                       /* current line */
static char *tp;                     /* current position in the line */
static u8 running, bye, loaded;
static const char *errmsg;

typedef struct { u16 vi; f32 lim, step; u8 *line; char *tp; } forent;
static forent fors[FORMAX];
static u8 nfor;
typedef struct { u8 *line; char *tp; } gosent;
static gosent gos[GOSMAX];
static u8 ngos;
static u8 *dline;                    /* DATA pointer */
static char *dtp;
static u32 rseed = 0x2545F491UL;

#define LNUM(p)  (((u16)(p)[0] << 8) | (p)[1])
#define LLEN(p)  ((p)[2])
#define LTEXT(p) ((char *)(p) + 3)
#define NEXTLINE(p) ((p) + 3 + LLEN(p))

static void fail(const char *m) { if (!errmsg) errmsg = m; }

static void clear_vars(void)
{
    u16 i;
    for (i = 0; i < NVARS; i++) numv[i] = 0;
    memset(strv, 0, sizeof(strv));
    memset(arr, 0, sizeof(arr));
    aused = 0;
    nfor = ngos = 0;
    dline = 0;
    dtp = 0;
}

/* ---- text scanning */
static void skipsp(void) { while (*tp == ' ') tp++; }

static int kw(const char *k)               /* consume the keyword k if it is next */
{
    const char *p = tp;
    while (*p == ' ') p++;
    while (*k) {
        if (*p != *k) return 0;
        p++;
        k++;
    }
    tp = (char *)p;
    return 1;
}

static int isalpha_(int c) { return c >= 'A' && c <= 'Z'; }
static int isalnum_(int c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

/* ---- values: the result of an expression is left in vt (0 number, 1 string), vf and vs
 * (CMOC passes structures by value in a very expensive way, so no structure is returned) */
static u8 vt;
static f32 vf;
static char *vs;

static char tmp[4][STRL];
static u8 tmpi;

static char *newtmp(void)
{
    tmpi = (u8)((tmpi + 1) & 3);
    return tmp[tmpi];
}

static void setn(f32 f) { vt = 0; vf = f; vs = 0; }

static void sets(char *s) { vt = 1; vf = 0; vs = s; }

static void expr(void);

static int nameidx(void)                   /* reads a name at tp: returns the index (0..NVARS-1) */
{
    u8 c1 = (u8)(*tp - 'A'), c2 = 0;
    char c;
    tp++;
    c = *tp;
    if (c >= '0' && c <= '9') c2 = (u8)(1 + (c - '0'));
    else if (isalpha_(c)) c2 = (u8)(11 + (c - 'A'));
    while (isalnum_(*tp)) tp++;
    return (int)((u16)c1 * 37 + c2);
}

static f32 vnum(void)                      /* the result as a number */
{
    if (vt) { fail("Type mismatch"); return 0; }
    return vf;
}

static f32 getnum(void)
{
    expr();
    return vnum();
}

static s32 getint(void)
{
    return f_to_long(getnum());
}

static void dimarr(u8 a, u16 d1, u16 d2, u8 dim)
{
    u32 n = (u32)(d1 + 1) * (u32)(d2 + 1);
    u16 i;
    if (arr[a].dim) { fail("Redimensioned array"); return; }
    if (d1 > 255 || d2 > 255 || aused + n > APOOL) { fail("Out of memory"); return; }
    arr[a].off = aused;
    arr[a].d1 = (u8)d1;
    arr[a].d2 = (u8)d2;
    arr[a].dim = dim;
    for (i = 0; i < (u16)n; i++) apool[aused + i] = 0;
    aused += (u16)n;
}

/* address of the element a(i[,j]) of the array a; the '(' is consumed by the caller */
static f32 *elem(u8 a)
{
    s32 i = getint(), j = 0;
    u16 ix;
    if (*tp == ',') { tp++; j = getint(); }
    if (*tp == ')') tp++; else fail("Syntax error");
    if (errmsg) return &apool[0];
    if (!arr[a].dim) dimarr(a, 10, 0, 1);
    if (errmsg) return &apool[0];
    if (i < 0 || i > arr[a].d1 || j < 0 || j > arr[a].d2 || (arr[a].dim == 1 && j)) { fail("Subscript out of range"); return &apool[0]; }
    ix = (u16)((u16)i * (u16)(arr[a].d2 + 1) + (u16)j);
    return &apool[arr[a].off + ix];
}

/* ---- functions */
static const char fnames[] = "ABS\0INT\0SGN\0SQR\0SIN\0COS\0TAN\0ATN\0EXP\0LOG\0RND\0LEN\0ASC\0VAL\0CHR$\0STR$\0LEFT$\0RIGHT$\0MID$\0TIMER\0";
#define NFN 20

static int fnmatch(void)
{
    const char *n = fnames;
    int i;
    if (!isalpha_(tp[1]) || !isalpha_(tp[2])) return -1;      /* the shortest name has 3 letters: most variables end here */
    for (i = 0; i < NFN; i++) {
        const char *p = tp, *q = n;
        while (*q && *p == *q) { p++; q++; }
        if (!*q) { tp = (char *)p; return i; }
        n += strlen(n) + 1;
    }
    return -1;
}

static void numstr(f32 f, char *b)             /* like the BASICs: a space for the sign */
{
    char t[16];
    u8 n = 0;
    if (!(f & F_SIGN)) b[n++] = ' ';
    f_format(f, t);
    strcpy(b + n, t);
}

static f32 rnd01(void)
{
    rseed = rseed * 1664525UL + 1013904223UL;
    return f_mul(f_from_long((s32)((rseed >> 8) & 0xFFFFFFUL)), 0x33800000UL);   /* / 2^24 */
}

static void need(char c)
{
    skipsp();
    if (*tp == c) tp++; else fail("Syntax error");
}

static void func(int id)
{
    char *as, *s;
    u8 at;
    s32 n, m, p2 = 0;
    f32 f;
    if (id == 19) {                                 /* TIMER: seconds since the system started */
        setn(f_div(f_from_long((s32)mk_ticks()), f_from_long(TICK_HZ)));
        return;
    }
    if (id == 10) {                                 /* RND */
        skipsp();
        if (*tp == '(') { tp++; getnum(); need(')'); }
        setn(rnd01());
        return;
    }
    need('(');
    expr();
    at = vt;
    as = vs;
    f = vf;
    if (id >= 16) {                                 /* LEFT$ RIGHT$ MID$ */
        u8 has3 = 0;
        if (at != 1) fail("Type mismatch");
        need(',');
        n = getint();
        skipsp();
        if (*tp == ',') { tp++; p2 = getint(); has3 = 1; }
        need(')');
        s = newtmp();
        s[0] = 0;
        if (!errmsg) {
            m = (s32)strlen(as);
            if (id == 16) {
                if (n > m) n = m;
                if (n < 0) n = 0;
                memcpy(s, as, (u16)n);
                s[(u16)n] = 0;
            } else if (id == 17) {
                if (n > m) n = m;
                if (n < 0) n = 0;
                strcpy(s, as + (u16)(m - n));
            } else {                                 /* MID$(s, start [, len]) */
                if (n < 1) n = 1;
                if (n <= m) {
                    if (!has3 || p2 > m - (n - 1)) p2 = m - (n - 1);
                    if (p2 < 0) p2 = 0;
                    memcpy(s, as + (u16)(n - 1), (u16)p2);
                    s[(u16)p2] = 0;
                }
            }
        }
        sets(s);
        return;
    }
    need(')');
    if (id >= 11 && id <= 15) {
        if (id == 15) {                             /* STR$ */
            s = newtmp();
            if (at) fail("Type mismatch"); else numstr(f, s);
            sets(s);
            return;
        }
        if (id == 14) {                             /* CHR$ */
            s = newtmp();
            if (at) fail("Type mismatch");
            s[0] = (char)f_to_long(f);
            s[1] = 0;
            sets(s);
            return;
        }
        if (at != 1) { fail("Type mismatch"); setn(0); return; }
        if (id == 11) { setn(f_from_long((s32)strlen(as))); return; }       /* LEN */
        if (id == 12) { setn(f_from_long((s32)(u8)as[0])); return; }        /* ASC */
        {                                           /* VAL */
            u8 neg = 0;
            while (*as == ' ') as++;
            if (*as == '-') { neg = 1; as++; } else if (*as == '+') as++;
            as = (char *)f_parse(as, &f);
            if (neg) f = f_neg(f);
            setn(f);
            return;
        }
    }
    if (at) { fail("Type mismatch"); setn(0); return; }
    switch (id) {
    case 0: f = f_abs(f); break;
    case 1: f = f_int(f); break;
    case 2:
        n = f_cmp(f, 0);
        if (n > 0) f = F_ONE; else if (n < 0) f = F_SIGN | F_ONE; else f = 0;
        break;
#ifndef NOMATH
    case 3: if (f & F_SIGN) fail("Illegal function call"); f = f_sqr(f); break;
    case 4: f = f_sin(f); break;
    case 5: f = f_cos(f); break;
    case 6: { f32 c2 = f_cos(f); if (!c2) { sfp_err = 2; f = F_MAX; } else f = f_div(f_sin(f), c2); } break;
    case 7: f = f_atn(f); break;
    case 8: f = f_exp(f); break;
    case 9: if (f_cmp(f, 0) <= 0) fail("Illegal function call"); else f = f_log(f); break;
#else
    case 3: case 4: case 5: case 6: case 7: case 8: case 9: fail("Illegal function call"); break;
#endif
    }
    setn(f);
}

/* ---- expressions: each level leaves its result in vt, vf and vs */
static void checkerr(void)
{
    if (sfp_err) {
        fail(sfp_err == 2 ? "Division by zero" : sfp_err == 3 ? "Illegal function call" : "Overflow");
        sfp_err = 0;
    }
}

static void primary(void)
{
    char c;
    int id;
    setn(0);
    if (errmsg) return;
    skipsp();
    c = *tp;
    if (c == '(') {
        tp++;
        expr();
        need(')');
        return;
    }
    if ((c >= '0' && c <= '9') || c == '.') {
        f32 f;
        tp = (char *)f_parse(tp, &f);
        setn(f);
        return;
    }
    if (c == '"') {
        char *s = newtmp();
        u8 n = 0;
        tp++;
        while (*tp && *tp != '"') { if (n < STRL - 1) s[n++] = *tp; tp++; }
        s[n] = 0;
        if (*tp == '"') tp++;
        sets(s);
        return;
    }
    if (isalpha_(c)) {
        const char *start;
        int ix;
        id = fnmatch();
        if (id >= 0) { func(id); return; }
        start = tp;
        ix = nameidx();
        if (*tp == '$') {
            tp++;
            if (tp - start > 2) fail("Syntax error");        /* string names: one letter */
            sets(strv[(u8)(*start - 'A')]);
            return;
        }
        skipsp();
        if (*tp == '(') {
            tp++;
            setn(*elem((u8)(*start - 'A')));
            return;
        }
        setn(numv[ix]);
        return;
    }
    fail("Syntax error");
}

static void power(void)
{
    f32 x, y;
    primary();
    skipsp();
    while (*tp == '^' && !errmsg) {
        tp++;
        x = vnum();
        primary();
        y = vnum();
        if (errmsg) break;
        if (!x) { if (y) setn(0); else setn(F_ONE); }
        else if (f_int(y) == y && f_cmp(f_abs(y), f_n(1000)) < 0) {
            s32 k = f_to_long(f_abs(y));
            f32 r = F_ONE, p = x;
            while (k) { if (k & 1) r = f_mul(r, p); k >>= 1; if (k) p = f_mul(p, p); }
            if (y & F_SIGN) r = f_div(F_ONE, r);
            setn(r);
        } else if (x & F_SIGN) fail("Illegal function call");
#ifndef NOMATH
        else setn(f_exp(f_mul(y, f_log(x))));
#else
        else fail("Illegal function call");
#endif
        skipsp();
    }
}

static void unary(void)
{
    skipsp();
    if (*tp == '-') { tp++; unary(); setn(f_neg(vnum())); return; }
    if (*tp == '+') { tp++; unary(); return; }
    power();
}

static void term(void)
{
    f32 a;
    char op;
    unary();
    for (;;) {
        skipsp();
        op = *tp;
        if (errmsg || (op != '*' && op != '/')) return;
        tp++;
        a = vnum();
        unary();
        if (op == '*') setn(f_mul(a, vnum())); else setn(f_div(a, vnum()));
        checkerr();
    }
}

static void sum(void)
{
    f32 a;
    char *as;
    char op;
    term();
    for (;;) {
        skipsp();
        op = *tp;
        if (errmsg || (op != '+' && op != '-')) return;
        tp++;
        if (vt) {
            u16 la, lb;
            char *s;
            as = vs;
            if (op == '-') { fail("Type mismatch"); return; }
            term();
            if (!vt) { fail("Type mismatch"); return; }
            la = (u16)strlen(as);
            lb = (u16)strlen(vs);
            if (la + lb >= STRL) { fail("String too long"); return; }
            s = newtmp();
            memcpy(s, as, la);
            memcpy(s + la, vs, lb);
            s[la + lb] = 0;
            sets(s);
        } else {
            a = vf;
            term();
            if (op == '+') setn(f_add(a, vnum())); else setn(f_sub(a, vnum()));
        }
        checkerr();
    }
}

static void compare(void)
{
    u8 at, op = 0;
    f32 af;
    char *as;
    int c, r;
    sum();
    skipsp();
    for (;;) {
        char ch = *tp;
        if (ch == '=') { op |= 2; tp++; }
        else if (ch == '<') { op |= 1; tp++; }
        else if (ch == '>') { op |= 4; tp++; }
        else break;
    }
    if (!op || errmsg) return;
    at = vt;
    af = vf;
    as = vs;
    sum();
    if (at != vt) { fail("Type mismatch"); return; }
    c = at ? strcmp(as, vs) : f_cmp(af, vf);
    r = (c < 0 && (op & 1)) || (c == 0 && (op & 2)) || (c > 0 && (op & 4));
    if (r) setn(F_SIGN | F_ONE); else setn(0);              /* true is -1 */
}

static void notv(void)
{
    skipsp();
    if (*tp == 'N' && kw("NOT")) { notv(); setn(f_from_long(~f_to_long(vnum()))); return; }
    compare();
}

static void andv(void)
{
    s32 a;
    notv();
    skipsp();
    while (!errmsg && *tp == 'A' && kw("AND")) {
        a = f_to_long(vnum());
        notv();
        setn(f_from_long(a & f_to_long(vnum())));
        skipsp();
    }
}

static void expr(void)
{
    s32 a;
    andv();
    skipsp();
    while (!errmsg && *tp == 'O' && kw("OR")) {
        a = f_to_long(vnum());
        andv();
        setn(f_from_long(a | f_to_long(vnum())));
        skipsp();
    }
}

/* ---- program lines */
static u8 *findline(u16 n)                 /* first line with a number >= n, or pend */
{
    u8 *p = pool;
    while (p < pend && LNUM(p) < n) p = NEXTLINE(p);
    return p;
}

static void setline(u16 n, const char *text)      /* insert, replace or delete a line */
{
    u8 *p = findline(n);
    u16 len = (u16)strlen(text) + 1;
    if (p < pend && LNUM(p) == n) {
        u8 *q = NEXTLINE(p);
        memmove(p, q, (u16)(pend - q));
        pend -= q - p;
    }
    if (*text == 0) return;
    if (len > 250 || (u16)(pend - pool) + len + 3 > PGM) { fail("Out of memory"); return; }
    memmove(p + len + 3, p, (u16)(pend - p));
    p[0] = (u8)(n >> 8);
    p[1] = (u8)n;
    p[2] = (u8)len;
    memcpy(p + 3, text, len);
    pend += len + 3;
}

static void upcase(char *s)                /* upper case outside the quotes */
{
    u8 q = 0;
    for (; *s; s++) {
        if (*s == '"') q ^= 1;
        else if (!q && *s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    }
}

static void jump(u16 n)
{
    u8 *p = findline(n);
    if (p >= pend || LNUM(p) != n) { fail("Undefined line"); return; }
    cl = p;
    tp = LTEXT(p);
}

static u16 getlineno(void)
{
    u16 n = 0;
    skipsp();
    if (*tp < '0' || *tp > '9') { fail("Syntax error"); return 0; }
    while (*tp >= '0' && *tp <= '9') { n = (u16)(n * 10 + (*tp - '0')); tp++; }
    return n;
}

static void skiprest(void) { while (*tp) tp++; }

/* ---- the statements */
static void st_print(void)
{
    char b[24];
    u8 nl = 1;
    for (;;) {
        skipsp();
        if (!*tp || *tp == ':' ) break;
        if (kw("ELSE")) { tp -= 4; break; }
        nl = 1;
        if (kw("TAB(")) { s32 n = getint(); if (*tp == ')') tp++; while (col < n && !errmsg) outc(' '); continue; }
        if (kw("SPC(")) { s32 n = getint(); if (*tp == ')') tp++; while (n-- > 0) outc(' '); continue; }
        if (*tp == ';') { tp++; nl = 0; continue; }
        if (*tp == ',') { tp++; nl = 0; do outc(' '); while (col % 14); continue; }
        expr();
        if (errmsg) return;
        if (vt) outs(vs);
        else { numstr(vf, b); outs(b); outc(' '); }
    }
    if (nl) outc('\n');
}

static void st_input(void)
{
    char line[LINEMAX], *p;
    int r;
    skipsp();
    if (*tp == '"') {
        primary();
        outs(vs);
        skipsp();
        if (*tp == ';' || *tp == ',') tp++;
    } else outs("? ");
    flush();
    r = gets_(line, sizeof(line));
    col = 0;
    if (r < 0) { fail("End of input"); return; }
    upcase(line);
    p = line;
    for (;;) {
        skipsp();
        if (!isalpha_(*tp)) { fail("Syntax error"); return; }
        {
            const char *start = tp;
            int ix = nameidx();
            u8 isstr = 0;
            f32 *dst = &numv[ix];
            char *sdst = 0;
            while (*p == ' ') p++;
            if (*tp == '$') { tp++; isstr = 1; sdst = strv[(u8)(*start - 'A')]; }
            else if (*tp == '(') { tp++; dst = elem((u8)(*start - 'A')); }
            if (isstr) {
                u8 n = 0;
                while (*p && *p != ',' && n < STRL - 1) sdst[n++] = *p++;
                sdst[n] = 0;
            } else {
                u8 neg = 0;
                f32 f;
                if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
                p = (char *)f_parse(p, &f);
                *dst = neg ? f_neg(f) : f;
            }
        }
        if (*p == ',') p++;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        break;
    }
}

static void st_let(void)
{
    const char *start = tp;
    int ix = nameidx();
    f32 *dst = &numv[ix];
    char *sdst = 0;
    skipsp();
    if (*tp == '$') { tp++; sdst = strv[(u8)(*start - 'A')]; skipsp(); }
    else if (*tp == '(') { tp++; dst = elem((u8)(*start - 'A')); skipsp(); }
    if (*tp != '=') { fail("Syntax error"); return; }
    tp++;
    expr();
    if (errmsg) return;
    if (sdst) {
        if (!vt) { fail("Type mismatch"); return; }
        strcpy(sdst, vs);
    } else {
        *dst = vnum();
    }
}

static void st_if(void)
{
    u8 t;
    expr();
    if (errmsg) return;
    t = vt ? 1 : (vf != 0 && F_EXP(vf) != 0);
    skipsp();
    if (!kw("THEN") && !kw("GOTO")) { fail("Syntax error"); return; }
    if (t) {
        skipsp();
        if (*tp >= '0' && *tp <= '9') jump(getlineno());
        return;                                   /* the statements after THEN follow */
    }
    for (;;) {                                    /* look for ELSE */
        u8 q = 0;
        char *p = tp;
        for (; *p; p++) {
            if (*p == '"') q ^= 1;
            else if (!q && p[0] == 'E' && p[1] == 'L' && p[2] == 'S' && p[3] == 'E') break;
        }
        if (*p) {
            tp = p + 4;
            skipsp();
            if (*tp >= '0' && *tp <= '9') jump(getlineno());
        } else skiprest();
        return;
    }
}

static void st_for(void)
{
    forent *e;
    int ix;
    f32 lim, step = F_ONE, init;
    skipsp();
    if (!isalpha_(*tp)) { fail("Syntax error"); return; }
    ix = nameidx();
    skipsp();
    if (*tp != '=') { fail("Syntax error"); return; }
    tp++;
    init = getnum();
    if (!kw("TO")) { fail("Syntax error"); return; }
    lim = getnum();
    if (kw("STEP")) step = getnum();
    if (errmsg) return;
    numv[ix] = init;
    for (e = fors; e < fors + nfor; e++)
        if (e->vi == (u16)ix) { nfor = (u8)(e - fors); break; }
    if (nfor >= FORMAX) { fail("FOR nested too deep"); return; }
    e = &fors[nfor++];
    e->vi = (u16)ix;
    e->lim = lim;
    e->step = step;
    e->line = cl;
    e->tp = tp;
}

static void st_next(void)
{
    for (;;) {
        forent *e;
        int ix = -1;
        f32 v;
        int c;
        skipsp();
        if (isalpha_(*tp)) ix = nameidx();
        if (!nfor) { fail("NEXT without FOR"); return; }
        if (ix >= 0) {
            while (nfor && fors[nfor - 1].vi != (u16)ix) nfor--;
            if (!nfor) { fail("NEXT without FOR"); return; }
        }
        e = &fors[nfor - 1];
        v = f_add(numv[e->vi], e->step);
        numv[e->vi] = v;
        c = f_cmp(v, e->lim);
        if ((e->step & F_SIGN) ? c >= 0 : c <= 0) {
            cl = e->line;
            tp = e->tp;
            return;
        }
        nfor--;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        return;
    }
}

static void st_dim(void)
{
    for (;;) {
        u8 a;
        s32 d1, d2 = 0;
        u8 dim = 1;
        skipsp();
        if (!isalpha_(*tp)) { fail("Syntax error"); return; }
        a = (u8)(*tp - 'A');
        nameidx();
        if (*tp != '(') { fail("Syntax error"); return; }
        tp++;
        d1 = getint();
        if (*tp == ',') { tp++; d2 = getint(); dim = 2; }
        if (*tp == ')') tp++; else { fail("Syntax error"); return; }
        if (d1 < 0 || d2 < 0) { fail("Illegal function call"); return; }
        dimarr(a, (u16)d1, (u16)d2, dim);
        if (errmsg) return;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        return;
    }
}

static void st_on(void)
{
    s32 n = getint();
    u8 sub;
    u8 *l;
    char *rest;
    if (kw("GOTO")) sub = 0; else if (kw("GOSUB")) sub = 1; else { fail("Syntax error"); return; }
    while (n > 1 && !errmsg) {                  /* skip n-1 line numbers */
        getlineno();
        skipsp();
        if (*tp == ',') tp++; else { return; }
        n--;
    }
    if (n < 1 || errmsg) { skiprest(); return; }
    {
        u16 target = getlineno();
        skipsp();
        while (*tp && *tp != ':') tp++;         /* the rest of the list */
        l = cl;
        rest = tp;
        if (sub) {
            if (ngos >= GOSMAX) { fail("Out of memory"); return; }
            gos[ngos].line = l;
            gos[ngos].tp = rest;
            ngos++;
        }
        jump(target);
    }
}

static int data_next(f32 *f, char *s, u8 *isstr)   /* next DATA item: 0 if none */
{
    for (;;) {
        if (!dline) { dline = pool; dtp = 0; }
        if (!dtp) {
            for (;;) {
                char *p;
                if (dline >= pend) return 0;
                p = LTEXT(dline);
                while (*p == ' ') p++;
                if (p[0] == 'D' && p[1] == 'A' && p[2] == 'T' && p[3] == 'A') { dtp = p + 4; break; }
                dline = NEXTLINE(dline);
            }
        }
        while (*dtp == ' ') dtp++;
        if (!*dtp) { dline = NEXTLINE(dline); dtp = 0; continue; }
        if (*dtp == ',') dtp++;
        while (*dtp == ' ') dtp++;
        if (!*dtp) { dline = NEXTLINE(dline); dtp = 0; continue; }
        if (*dtp == '"') {
            u8 n = 0;
            dtp++;
            while (*dtp && *dtp != '"' && n < STRL - 1) s[n++] = *dtp++;
            s[n] = 0;
            if (*dtp == '"') dtp++;
            *isstr = 1;
        } else {
            u8 neg = 0;
            char *q = dtp;
            u8 n = 0;
            while (*q && *q != ',') q++;
            while (n < STRL - 1 && dtp + n < q) { s[n] = dtp[n]; n++; }
            s[n] = 0;
            {
                const char *r = s;
                while (*r == ' ') r++;
                if (*r == '-') { neg = 1; r++; }
                f_parse(r, f);
                if (neg) *f = f_neg(*f);
            }
            dtp = q;
            *isstr = 0;
        }
        return 1;
    }
}

static void st_read(void)
{
    for (;;) {
        char s[STRL];
        f32 f = 0;
        u8 isstr;
        const char *start;
        int ix;
        f32 *dst;
        char *sdst = 0;
        skipsp();
        if (!isalpha_(*tp)) { fail("Syntax error"); return; }
        start = tp;
        ix = nameidx();
        dst = &numv[ix];
        if (*tp == '$') { tp++; sdst = strv[(u8)(*start - 'A')]; }
        else if (*tp == '(') { tp++; dst = elem((u8)(*start - 'A')); }
        if (!data_next(&f, s, &isstr)) { fail("Out of DATA"); return; }
        if (sdst) strcpy(sdst, s); else *dst = f;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        return;
    }
}

static void st_list(void)
{
    u16 a = 0, b = 0xFFFF;
    u8 *p;
    skipsp();
    if (*tp >= '0' && *tp <= '9') {
        a = getlineno();
        b = a;
        skipsp();
        if (*tp == '-') {
            tp++;
            skipsp();
            b = (*tp >= '0' && *tp <= '9') ? getlineno() : 0xFFFF;
        }
    } else if (*tp == '-') { tp++; b = getlineno(); }
    for (p = pool; p < pend; p = NEXTLINE(p)) {
        if (LNUM(p) < a || LNUM(p) > b) continue;
        outn(LNUM(p));
        outc(' ');
        outs(LTEXT(p));
        outc('\n');
    }
}

static void load_file(const char *name)
{
    int fd = open(name, FO_READ), r, i;
    static u8 buf[128];
    char line[LINEMAX];
    u16 len = 0;
    if (fd < 0) { fail("File not found"); return; }
    pend = pool;
    clear_vars();
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < r; i++) {
            u8 c = buf[i];
            if (c == '\n' || c == '\r') {
                if (len) {
                    char *p = line;
                    u16 n = 0;
                    line[len] = 0;
                    upcase(line);
                    while (*p == ' ') p++;
                    while (*p >= '0' && *p <= '9') { n = (u16)(n * 10 + (*p - '0')); p++; }
                    if (*p == ' ') p++;
                    if (n) setline(n, p);
                }
                len = 0;
            } else if (len < LINEMAX - 1) line[len++] = (char)c;
        }
    }
    close(fd);
}

static void save_file(const char *name)
{
    int fd = open(name, FO_WRITE | FO_CREATE | FO_TRUNC);
    u8 *p;
    char b[LINEMAX + 12];
    if (fd < 0) { fail("Cannot create file"); return; }
    for (p = pool; p < pend; p = NEXTLINE(p)) {
        u16 n = LNUM(p), l;
        char t[8];
        utoa_(n, t, 10);
        strcpy(b, t);
        strcat(b, " ");
        strcat(b, LTEXT(p));
        strcat(b, "\n");
        l = (u16)strlen(b);
        write(fd, b, l);
    }
    close(fd);
}

static void getname(char *dst)
{
    expr();
    if (errmsg) return;
    if (!vt) { fail("Type mismatch"); return; }
    strcpy(dst, vs);
}

/* one statement at tp */
static void st_gosub(void)
{
    u16 n = getlineno();
    if (errmsg) return;
    if (ngos >= GOSMAX) { fail("Out of memory"); return; }
    gos[ngos].line = cl;
    gos[ngos].tp = tp;
    ngos++;
    jump(n);
}

static void st_run(void)
{
    u16 n = 0;
    skipsp();
    if (*tp >= '0' && *tp <= '9') n = getlineno();
    clear_vars();
    if (pend == pool) { running = 0; return; }
    if (n) jump(n); else { cl = pool; tp = LTEXT(pool); }
    running = 1;
}

static void stmt(void)
{
    char name[STRL];
    skipsp();
    switch (*tp) {
    case '\'': skiprest(); return;
    case '?': tp++; st_print(); return;
    case 'R':
        if (kw("REM")) { skiprest(); return; }
        if (kw("RETURN")) {
            if (!ngos) { fail("RETURN without GOSUB"); return; }
            ngos--;
            cl = gos[ngos].line;
            tp = gos[ngos].tp;
            return;
        }
        if (kw("READ")) { st_read(); return; }
        if (kw("RESTORE")) { dline = 0; dtp = 0; return; }
        if (kw("RANDOMIZE")) { rseed ^= (u32)mk_ticks() * 2654435761UL; skiprest(); return; }
        if (kw("RUN")) { st_run(); return; }
        break;
    case 'P': if (kw("PRINT")) { st_print(); return; } break;
    case 'L':
        if (kw("LET")) { st_let(); return; }
        if (kw("LIST")) { st_list(); return; }
        if (kw("LOAD")) { getname(name); if (!errmsg) load_file(name); running = 0; return; }
        break;
    case 'I':
        if (kw("IF")) { st_if(); return; }
        if (kw("INPUT")) { st_input(); return; }
        break;
    case 'G':
        if (kw("GOTO")) { jump(getlineno()); return; }
        if (kw("GOSUB")) { st_gosub(); return; }
        break;
    case 'F': if (kw("FOR")) { st_for(); return; } break;
    case 'N':
        if (kw("NEXT")) { st_next(); return; }
        if (kw("NEW")) { pend = pool; clear_vars(); running = 0; return; }
        break;
    case 'E':
        if (kw("END")) { running = 0; return; }
        if (kw("ELSE")) { skiprest(); return; }
        break;
    case 'S':
        if (kw("STOP")) { running = 0; return; }
        if (kw("SAVE")) { getname(name); if (!errmsg) save_file(name); return; }
        if (kw("SYSTEM")) { bye = 1; running = 0; return; }
        break;
    case 'D':
        if (kw("DIM")) { st_dim(); return; }
        if (kw("DATA")) { skiprest(); return; }
        break;
    case 'C': if (kw("CLS")) { outs("\033[2J\033[H"); col = 0; return; } break;
    case 'O': if (kw("ON")) { st_on(); return; } break;
    case 'B': if (kw("BYE")) { bye = 1; running = 0; return; } break;
    case 'Q': if (kw("QUIT")) { bye = 1; running = 0; return; } break;
    }
    if (isalpha_(*tp)) { st_let(); return; }
    fail("Syntax error");
}

/* run from (cl, tp) until the program ends */
static void execute(void)
{
    running = 1;
    errmsg = 0;
    sfp_err = 0;
    while (running && !errmsg) {
        skipsp();
        if (*tp == ':') { tp++; continue; }
        if (!*tp) {
            if (cl == (u8 *)dbuf) { running = 0; break; }
            cl = NEXTLINE(cl);
            if (cl >= pend) { running = 0; break; }
            tp = LTEXT(cl);
            continue;
        }
        stmt();
        checkerr();
    }
    if (errmsg) {
        if (col) outc('\n');
        outc('?');
        outs(errmsg);
        if (cl != (u8 *)dbuf && cl < pend) { outs(" in "); outn(LNUM(cl)); }
        outc('\n');
        errmsg = 0;
    }
    running = 0;
}

static void direct(char *line)
{
    char *p = line;
    u16 n = 0;
    upcase(line);
    while (*p == ' ') p++;
    if (!*p) return;
    if (*p >= '0' && *p <= '9') {
        while (*p >= '0' && *p <= '9') { n = (u16)(n * 10 + (*p - '0')); p++; }
        if (*p == ' ') p++;
        errmsg = 0;
        setline(n, p);
        clear_vars();
        if (errmsg) { outs("?"); outs(errmsg); outc('\n'); errmsg = 0; }
        return;
    }
    /* the direct mode line: header like a program line so that errors and jumps work */
    dbuf[0] = 0;
    dbuf[1] = 0;
    dbuf[2] = (u8)(strlen(p) + 1);
    strcpy(dbuf + 3, p);
    cl = (u8 *)dbuf;
    tp = dbuf + 3;
    execute();
}

int main(int argc, char **argv)
{
    char line[LINEMAX];
    pend = pool;
    clear_vars();
    if (argc > 1) {
        errmsg = 0;
        load_file(argv[1]);
        if (errmsg) { outs("?"); outs(errmsg); outc('\n'); flush(); return 1; }
        if (pend == pool) return 0;
        cl = pool;
        tp = LTEXT(pool);
        execute();
        flush();
        return 0;
    }
    outs("MK09 BASIC 1.0  (c) 2026 YUKARI Semiconductor Devices\n");
    {
        char t[24];
        u16 n = (u16)(PGM - (u16)(pend - pool));
        utoa_(n, t, 10);
        outs(t);
        outs(" bytes free\n");
    }
    while (!bye) {
        int r;
        if (col) outc('\n');
        outs("Ok\n");
        flush();
        r = gets_(line, sizeof(line));
        col = 0;                            /* the console echoed the line and its end */
        if (r < 0) break;
        direct(line);
        flush();
    }
    flush();
    return 0;
}
