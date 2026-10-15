/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* sfpm.h - conversion from and to text, and the math functions of the BASIC (on sfp.h) */
#ifndef SFPM_H
#define SFPM_H

#include "sfp.h"

#define F_TWO   0x40000000UL
#define F_HALF  0x3F000000UL
#define F_TEN   0x41200000UL
#define F_PI    0x40490FDBUL
#define F_HPI   0x3FC90FDBUL
#define F_2PI   0x40C90FDBUL
#define F_LN2   0x3F317218UL
#define F_LOG2E 0x3FB8AA3BUL

static f32 f_n(s32 n) { return f_from_long(n); }

/* 10^n, n = 0..38 */
static f32 f_pow10(int n)
{
    f32 r = F_ONE, p = F_TEN;
    while (n) {
        if (n & 1) r = f_mul(r, p);
        p = f_mul(p, p);
        n >>= 1;
    }
    return r;
}

/* ---- text -> number: reads digits [. digits] [E[+-]digits]; returns the end */
static const char *f_parse(const char *s, f32 *out)
{
    u32 m = 0;
    int digits = 0, dec = 0, seen = 0, ex = 0, neg = 0, k;
    f32 r;
    while ((*s >= '0' && *s <= '9') || (*s == '.' && !seen)) {
        if (*s == '.') { seen = 1; s++; continue; }
        if (digits < 9) {
            m = m * 10 + (u32)(*s - '0');
            if (m) digits++;
            if (seen) dec++;
        } else if (!seen) dec--;                /* the digit is dropped, the value is 10 times bigger */
        s++;
    }
    if (*s == 'E' || *s == 'e') {
        const char *t = s + 1;
        if (*t == '+' || *t == '-') { neg = *t == '-'; t++; }
        if (*t >= '0' && *t <= '9') {
            while (*t >= '0' && *t <= '9') { if (ex < 1000) ex = ex * 10 + (*t - '0'); t++; }
            if (neg) ex = -ex;
            s = t;
        }
    }
    k = ex - dec;
    r = f_from_long((s32)m);
    if (!m) { *out = 0; return s; }
    while (k > 0) { int c = k > 38 ? 38 : k; r = f_mul(r, f_pow10(c)); k -= c; }
    while (k < 0) { int c = -k > 10 ? 10 : -k; r = f_div(r, f_pow10(c)); k += c; }
    *out = r;
    return s;
}

/* ---- number -> text (6 significant digits, like the BASICs of the time); returns the length.
 * A positive number gets no sign here, the caller puts the space. buf needs 16 bytes. */
static int f_format(f32 a, char *buf)
{
    char dg[8];
    int e10, i, n = 0, nd, k;
    s32 v;
    f32 x, lo, hi;
    if (!F_EXP(a)) { buf[0] = '0'; buf[1] = 0; return 1; }
    if (a & F_SIGN) { buf[n++] = '-'; a &= ~F_SIGN; }
    e10 = (int)(((long)(F_EXP(a) - 127) * 30103L) / 100000L);
    if (F_EXP(a) < 127) e10--;
    lo = f_n(100000L);
    hi = f_n(1000000L);
    for (i = 0; i < 4; i++) {
        k = 5 - e10;
        x = a;
        while (k > 0) { int c = k > 30 ? 30 : k; x = f_mul(x, f_pow10(c)); k -= c; }
        while (k < 0) { int c = -k > 30 ? 30 : -k; x = f_div(x, f_pow10(c)); k += c; }
        x = f_add(x, F_HALF);
        v = f_to_long(x);
        if (v >= 1000000L) { e10++; continue; }
        if (v < 100000L) { e10--; continue; }
        break;
    }
    (void)lo; (void)hi;
    for (i = 5; i >= 0; i--) { dg[i] = (char)('0' + (int)(v % 10)); v /= 10; }
    nd = 6;
    while (nd > 1 && dg[nd - 1] == '0') nd--;
    if (e10 >= 0 && e10 < 6) {
        for (i = 0; i <= e10; i++) buf[n++] = i < nd ? dg[i] : '0';
        if (nd > e10 + 1) {
            buf[n++] = '.';
            for (i = e10 + 1; i < nd; i++) buf[n++] = dg[i];
        }
    } else if (e10 < 0 && e10 >= -3) {
        buf[n++] = '0';
        buf[n++] = '.';
        for (i = -1; i > e10; i--) buf[n++] = '0';
        for (i = 0; i < nd; i++) buf[n++] = dg[i];
    } else {
        buf[n++] = dg[0];
        if (nd > 1) {
            buf[n++] = '.';
            for (i = 1; i < nd; i++) buf[n++] = dg[i];
        }
        buf[n++] = 'E';
        if (e10 < 0) { buf[n++] = '-'; e10 = -e10; } else buf[n++] = '+';
        buf[n++] = (char)('0' + e10 / 10);
        buf[n++] = (char)('0' + e10 % 10);
    }
    buf[n] = 0;
    return n;
}

/* ---- math */
#ifndef NOMATH
static f32 f_sqr(f32 a)
{
    f32 x;
    int i;
    if (!F_EXP(a) || (a & F_SIGN)) return 0;
    x = ((a >> 1) + 0x1FC00000UL);
    for (i = 0; i < 4; i++) x = f_mul(f_add(x, f_div(a, x)), F_HALF);
    return x;
}

/* sin on [-pi/2, pi/2] */
static f32 f_sin_core(f32 x)
{
    f32 x2 = f_mul(x, x), t = x, s = x;
    static const long inv[5] = { 6, 20, 42, 72, 110 };      /* t = -t * x2 / ((2k)(2k+1)) */
    int k;
    for (k = 0; k < 5; k++) {
        t = f_neg(f_div(f_mul(t, x2), f_n(inv[k])));
        s = f_add(s, t);
    }
    return s;
}

static f32 f_sin(f32 x)
{
    f32 q;
    u8 neg = 0;
    if (x & F_SIGN) { neg = 1; x &= ~F_SIGN; }
    if (f_cmp(x, f_n(100000L)) > 0) return 0;
    q = f_int(f_div(x, F_2PI));
    x = f_sub(x, f_mul(q, F_2PI));                  /* 0 .. 2pi */
    if (f_cmp(x, F_PI) > 0) { x = f_sub(x, F_PI); neg ^= 1; }   /* sin(x) = -sin(x - pi) */
    if (f_cmp(x, F_HPI) > 0) x = f_sub(F_PI, x);
    x = f_sin_core(x);
    if (neg) return f_neg(x);
    return x;
}

static f32 f_cos(f32 x) { return f_sin(f_add(f_abs(x), F_HPI)); }

static f32 f_atn(f32 x)
{
    u8 neg = (u8)(x >> 31), inv = 0;
    f32 x2, t, s;
    int k;
    x &= ~F_SIGN;
    if (f_cmp(x, F_ONE) > 0) { x = f_div(F_ONE, x); inv = 1; }
    for (k = 0; k < 2; k++)                         /* atn(x) = 2 atn(x / (1 + sqr(1 + x^2))) */
        x = f_div(x, f_add(F_ONE, f_sqr(f_add(F_ONE, f_mul(x, x)))));
    x2 = f_mul(x, x);
    t = x;
    s = x;
    for (k = 3; k <= 11; k += 2) {
        t = f_neg(f_mul(t, x2));
        s = f_add(s, f_div(t, f_n(k)));
    }
    s = f_mul(s, f_n(4));
    if (inv) s = f_sub(F_HPI, s);
    if (neg) return f_neg(s);
    return s;
}

static f32 f_exp(f32 x)
{
    s32 k;
    f32 r, t, s;
    int i, e;
    if (f_cmp(x, f_n(88)) > 0) { sfp_err = 1; return F_MAX; }
    if (f_cmp(x, f_n(-87)) < 0) return 0;
    k = f_to_long(f_int(f_add(f_mul(x, F_LOG2E), F_HALF)));
    r = f_sub(x, f_mul(f_n(k), F_LN2));             /* |r| <= 0.35 */
    t = F_ONE;
    s = F_ONE;
    for (i = 1; i <= 8; i++) {
        t = f_div(f_mul(t, r), f_n(i));
        s = f_add(s, t);
    }
    e = F_EXP(s) + (int)k;
    if (e <= 0) return 0;
    if (e >= 255) { sfp_err = 1; return F_MAX; }
    return (s & ~(0xFFUL << 23)) | ((u32)e << 23);
}

static f32 f_log(f32 a)
{
    int e;
    f32 m, s, s2, t, r;
    int k;
    if (!F_EXP(a) || (a & F_SIGN)) { sfp_err = 3; return 0; }
    e = F_EXP(a) - 127;
    m = (a & 0x7FFFFFUL) | F_ONE;                   /* 1 .. 2 */
    if (f_cmp(m, 0x3FB504F3UL) > 0) { m = f_mul(m, F_HALF); e++; }   /* 0.707 .. 1.414 */
    s = f_div(f_sub(m, F_ONE), f_add(m, F_ONE));
    s2 = f_mul(s, s);
    t = s;
    r = s;
    for (k = 3; k <= 9; k += 2) {
        t = f_mul(t, s2);
        r = f_add(r, f_div(t, f_n(k)));
    }
    r = f_mul(r, F_TWO);
    return f_add(r, f_mul(f_n(e), F_LN2));
}

#endif   /* NOMATH */

#endif
