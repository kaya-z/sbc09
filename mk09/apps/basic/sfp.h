/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * sfp.h - 32 bit floating point in software for the BASIC of MK09 (CMOC has no floating point
 * for this target).  The format is IEEE 754 single precision: 1 sign bit, 8 bit exponent (bias 127),
 * 23 bit fraction.  Results are rounded to nearest even.  There are no NaNs and infinities and no
 * denormals: a result that is too small is 0, one that is too big sets sfp_err (the result is then the
 * biggest number).  Functions: add sub mul div, comparison, conversion from and to long, int(),
 * and in sfpm.h sqr sin cos atn exp log.
 *
 * With MK_HOST defined it is built on the PC (tests/sfp_test.c compares it with the float of the C compiler).
 */
#ifndef SFP_H
#define SFP_H

#ifdef MK_HOST
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint8_t u8;
#else
#include "types.h"
#endif

typedef u32 f32;

#define F_SIGN  0x80000000UL
#define F_ZERO  0UL
#define F_ONE   0x3F800000UL
#define F_MAX   0x7F7FFFFFUL

#ifdef SFP_ASM
/* f_add f_mul f_div f_from_long are in assembler (sfpa.s); the C versions below are the reference */
extern u8 sfp_err;
f32 f_add(f32 a, f32 b);
f32 f_mul(f32 a, f32 b);
f32 f_div(f32 a, f32 b);
f32 f_from_long(s32 n);
#else
static u8 sfp_err;          /* 1 after an overflow, 2 after a division by zero (set by the caller to 0) */
#endif

#define F_EXP(a)  ((int)(((a) >> 23) & 0xFF))
#define F_MANT(a) (((a) & 0x7FFFFFUL) | 0x800000UL)

/* shifts by a variable count: the CMOC code for  m >>= n  inside an if() was wrong in one case, so these are loops */
static u32 shr32(u32 m, int n)
{
    while (n >= 8) { m >>= 8; n -= 8; }
    while (n > 0) { m >>= 1; n--; }
    return m;
}

static u32 shl32(u32 m, int n)
{
    while (n >= 8) { m <<= 8; n -= 8; }
    while (n > 0) { m <<= 1; n--; }
    return m;
}

static f32 f_neg(f32 a)
{
    if (!F_EXP(a)) return 0;
    return a ^ F_SIGN;
}
static f32 f_abs(f32 a) { return a & ~F_SIGN; }

#ifndef SFP_ASM
/* round and pack: m has the hidden bit at bit 26 (24 bits + 3 guard bits, the lowest being sticky) */
static f32 f_round(u8 sign, int e, u32 m)
{
    u8 g = (u8)(m & 7);
    m >>= 3;
    if (g > 4 || (g == 4 && (m & 1))) {
        m++;
        if (m == 0x1000000UL) { m >>= 1; e++; }
    }
    if (e >= 255) {
        sfp_err = 1;
        if (sign) return F_SIGN | F_MAX;
        return F_MAX;
    }
    if (e <= 0) return 0;
    return ((u32)sign << 31) | ((u32)e << 23) | (m & 0x7FFFFFUL);
}

static f32 f_add(f32 a, f32 b)
{
    int ea = F_EXP(a), eb = F_EXP(b), d;
    u32 ma, mb, t;
    u8 sa, sb;
    if (!ea) return b;
    if (!eb) return a;
    if (ea < eb || (ea == eb && F_MANT(a) < F_MANT(b))) { t = a; a = b; b = t; d = ea; ea = eb; eb = d; }
    sa = (u8)(a >> 31);
    sb = (u8)(b >> 31);
    d = ea - eb;
    if (d > 26) return a;
    ma = F_MANT(a) << 3;
    mb = F_MANT(b) << 3;
    if (d) {
        u32 lost = mb & (shl32(1, d) - 1);
        mb = shr32(mb, d);
        if (lost) mb |= 1;
    }
    if (sa == sb) ma += mb; else ma -= mb;
    if (!ma) return 0;
    if (ma >= 0x8000000UL) {
        ma = (ma >> 1) | (ma & 1);
        ea++;
    } else {
        while (ma < 0x4000000UL) { ma <<= 1; ea--; }
    }
    return f_round(sa, ea, ma);
}


static f32 f_mul(f32 a, f32 b)
{
    int ea = F_EXP(a), eb = F_EXP(b), e;
    u32 ma, mb, ah, al, bh, bl, hh, mid, low, hi, lo, m;
    u8 sign, g, st;
    if (!ea || !eb) return 0;
    sign = (u8)((a ^ b) >> 31);
    ma = F_MANT(a);
    mb = F_MANT(b);
    ah = ma >> 12; al = ma & 0xFFF;
    bh = mb >> 12; bl = mb & 0xFFF;
    hh = ah * bh;
    mid = ah * bl + al * bh;
    low = al * bl + ((mid & 0xFFF) << 12);
    hi = hh + (mid >> 12) + (low >> 24);
    lo = low & 0xFFFFFFUL;                     /* product = hi * 2^24 + lo, 2^46 <= product < 2^48 */
    e = ea + eb - 127;
    if (hi >= 0x800000UL) {                    /* mantissa = hi, remainder = lo (24 bits) */
        m = hi;
        e++;
        g = (u8)((lo >> 23) & 1);
        st = (u8)((lo & 0x7FFFFFUL) != 0);
    } else {                                   /* mantissa = hi*2 + top bit of lo, remainder 23 bits */
        m = (hi << 1) | (lo >> 23);
        g = (u8)((lo >> 22) & 1);
        st = (u8)((lo & 0x3FFFFFUL) != 0);
    }
    return f_round(sign, e, (m << 3) | ((u32)g << 2) | st);
}

static f32 f_div(f32 a, f32 b)
{
    int ea = F_EXP(a), eb = F_EXP(b), e, i;
    u32 ma, mb, r, q;
    u8 sign;
    if (!eb) { sfp_err = 2; return F_MAX; }
    if (!ea) return 0;
    sign = (u8)((a ^ b) >> 31);
    ma = F_MANT(a);
    mb = F_MANT(b);
    e = ea - eb + 127;
    r = ma;
    if (r < mb) { r <<= 1; e--; }
    q = 0;
    for (i = 0; i < 27; i++) {                 /* 24 bits + 3 guard bits */
        q <<= 1;
        if (r >= mb) { r -= mb; q |= 1; }
        r <<= 1;
    }
    if (r) q |= 1;                             /* sticky */
    return f_round(sign, e, q);
}

#endif   /* SFP_ASM */

static f32 f_sub(f32 a, f32 b) { return f_add(a, f_neg(b)); }

/* -1, 0, 1 */
static int f_cmp(f32 a, f32 b)
{
    u8 sa, sb;
    if (!F_EXP(a)) a = 0;
    if (!F_EXP(b)) b = 0;
    if (a == b) return 0;
    sa = (u8)(a >> 31);
    sb = (u8)(b >> 31);
    if (!a) return sb ? 1 : -1;
    if (!b) return sa ? -1 : 1;
    if (sa != sb) return sa ? -1 : 1;
    if (sa) return a > b ? -1 : 1;
    return a > b ? 1 : -1;
}

#ifndef SFP_ASM
static f32 f_from_long(s32 n)
{
    u32 m;
    int e = 150;
    u8 sign = 0, g;
    if (!n) return 0;
    if (n < 0) { sign = 1; m = (u32)(-n); } else m = (u32)n;
    if (m >= 0x1000000UL) {                    /* more than 24 bits: drop s bits with rounding */
        int sh = 0;
        u32 rem;
        while (shr32(m, sh) >= 0x1000000UL) sh++;
        rem = m & (shl32(1, sh) - 1);
        m = shr32(m, sh);
        e += sh;
        if (sh >= 3) g = (u8)(shr32(rem, sh - 3) | ((rem & (shl32(1, sh - 3) - 1)) != 0));
        else g = (u8)shl32(rem, 3 - sh);
        return f_round(sign, e, (m << 3) | g);
    }
    while (m < 0x800000UL) { m <<= 1; e--; }
    return f_round(sign, e, m << 3);
}

#endif   /* SFP_ASM */

/* truncate towards zero; saturates */
static s32 f_to_long(f32 a)
{
    int e = F_EXP(a);
    u32 m;
    if (e < 127) return 0;
    e -= 150;
    m = F_MANT(a);
    if (e > 7) m = 0x7FFFFFFFUL;               /* too big */
    else if (e >= 0) m = shl32(m, e);
    else m = shr32(m, -e);
    if (m > 0x7FFFFFFFUL) m = 0x7FFFFFFFUL;
    if (a & F_SIGN) return -(s32)m;
    return (s32)m;
}

/* the biggest whole number not greater than a */
static f32 f_int(f32 a)
{
    int e = F_EXP(a);
    u32 mask, m;
    if (e >= 150 || !e) return a;
    if (e < 127) {
        if (a & F_SIGN) return F_SIGN | F_ONE;
        return 0;
    }
    mask = shl32(1, 150 - e) - 1;
    m = a & 0x7FFFFFUL;
    if (!(m & mask)) return a;
    a &= ~mask;
    if (a & F_SIGN) a = f_sub(a, F_ONE);
    return a;
}

#endif
