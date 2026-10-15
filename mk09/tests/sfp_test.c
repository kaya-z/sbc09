/* SPDX-License-Identifier: MIT */
/* sfp_test.c - compare apps/basic/sfp.h with the float of the C compiler (host test, make sfptest) */
#define MK_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../apps/basic/sfpm.h"

static f32 tof(float f) { f32 u; memcpy(&u, &f, 4); return u; }
static float fromf(f32 u) { float f; memcpy(&f, &u, 4); return f; }
static int bad;
static u32 rs = 12345;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static float rf(void)
{
    u32 r = rnd();
    int e = (int)(rnd() % 80) + 87;           /* 2^-40 .. 2^39 */
    u32 u = (r & 0x80000000u) | ((u32)e << 23) | (r & 0x7FFFFF);
    if ((rnd() & 7) == 0) u &= ~0x7FFFFFu;    /* powers of two */
    if ((rnd() & 15) == 0) return 0.0f;
    return fromf(u);
}

static void chk(const char *op, float a, float b, f32 got, float want)
{
    f32 w = tof(want);
    if (got != w && !(got == 0 && fabsf(want) < 1.2e-38f)) {
        if (bad < 20) printf("%s(%g,%g): got %08x want %08x\n", op, a, b, (unsigned)got, (unsigned)w);
        bad++;
    }
}

int main(void)
{
    int i;
    for (i = 0; i < 300000; i++) {
        float a = rf(), b = rf();
        volatile float s = a + b, d = a - b, m = a * b, q = b != 0 ? a / b : 0;
        sfp_err = 0;
        if (!(fabsf(s) > 3e38f)) chk("add", a, b, f_add(tof(a), tof(b)), s);
        if (!(fabsf(d) > 3e38f)) chk("sub", a, b, f_sub(tof(a), tof(b)), d);
        if (!(fabsf(m) > 3e38f)) chk("mul", a, b, f_mul(tof(a), tof(b)), m);
        if (b != 0 && !(fabsf(q) > 3e38f)) chk("div", a, b, f_div(tof(a), tof(b)), q);
        {
            int c = a < b ? -1 : a > b ? 1 : 0;
            if (f_cmp(tof(a), tof(b)) != c) { if (bad < 20) printf("cmp(%g,%g)\n", a, b); bad++; }
        }
        if (fabsf(a) < 2e9f) {
            if (f_to_long(tof(a)) != (s32)a) { if (bad < 20) printf("to_long(%g) %d %d\n", a, (int)f_to_long(tof(a)), (int)a); bad++; }
            chk("int", a, 0, f_int(tof(a)), floorf(a));
        }
    }
    for (i = 0; i < 100000; i++) {
        s32 n = (s32)rnd();
        if (i & 1) n >>= (rnd() % 31);
        if (n == (s32)0x80000000) continue;
        chk("ltof", (float)n, 0, f_from_long(n), (float)n);
    }
    {   /* the functions: relative error 1e-6 (absolute near the zeros) */
        double worst = 0; int j;
        static const char *nm[] = { "sqr", "sin", "cos", "atn", "exp", "log" };
        for (j = 0; j < 6; j++) {
            int n; double w = 0;
            for (n = 0; n < 20000; n++) {
                float x = (float)((double)(rnd() % 2000000) / 100000.0 - 10.0), want; f32 got;
                if (j == 0) { x = fabsf(x) * (rnd() % 1000); want = sqrtf(x); got = f_sqr(tof(x)); }
                else if (j == 1) { want = sinf(x); got = f_sin(tof(x)); }
                else if (j == 2) { want = cosf(x); got = f_cos(tof(x)); }
                else if (j == 3) { x *= (rnd() & 1) ? 1 : 30; want = atanf(x); got = f_atn(tof(x)); }
                else if (j == 4) { want = expf(x); got = f_exp(tof(x)); }
                else { x = fabsf(x) * (rnd() % 100000) / 1000 + 1e-3f; want = logf(x); got = f_log(tof(x)); }
                { double err = fabs(fromf(got) - want) / (fabs(want) > 1 ? fabs(want) : 1); if (err > w) w = err; }
            }
            printf("%s: worst error %.2e\n", nm[j], w);
            if (w > worst) worst = w;
        }
        if (worst > 3e-6) bad++;
    }
    {   /* text */
        static const char *t[] = { "0", "1", "3.14159", "0.0458", "1E10", "123456789", "1.5E-7", "255", ".5", "99999999", "0.00012345", "1E38", "16777216" };
        int j; char b[24];
        for (j = 0; j < 13; j++) {
            f32 v; const char *e = f_parse(t[j], &v);
            f_format(v, b);
            printf("%-12s -> %08x %s%s\n", t[j], (unsigned)v, b, *e ? " (REST)" : "");
        }
    }
    printf("%s (%d errors)\n", bad ? "FAIL" : "sfp ok", bad);
    return bad != 0;
}
