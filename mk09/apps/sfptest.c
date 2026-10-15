/* SPDX-License-Identifier: MIT */
/* sfptest - checks the soft floating point of the BASIC on the target against results made on the PC
 * (tests/sfp_vectors.h: the C float of the PC; see tests/mk09_test.py) */
#include "types.h"
#include "mklib.h"
#define SFP_ASM
#include "sfpm.h"
#include "sfp_vectors.h"

static u8 bad;

static void chk(const char *op, u16 i, f32 got, f32 want)
{
    if (got != want) {
        printf("%s %d: %x%x want %x%x\n", op, i, (u16)(got >> 16), (u16)got, (u16)(want >> 16), (u16)want);
        bad++;
    }
}

int main(int argc, char **argv)
{
    u16 i;
    char buf[20];
    f32 r;
    for (i = 0; i < NTV; i++) {
        const u32 *t = tv[i];
        sfp_err = 0; r = f_add(t[0], t[1]); if (sfp_err) r = 0xFFFFFFFFUL; chk("add", i, r, t[2]);
        sfp_err = 0; r = f_mul(t[0], t[1]); if (sfp_err) r = 0xFFFFFFFFUL; chk("mul", i, r, t[3]);
        sfp_err = 0; r = f_div(t[0], t[1]); if (sfp_err) r = 0xFFFFFFFFUL; chk("div", i, r, t[4]);
        chk("long", i, (u32)f_to_long(t[0]), t[5]);
        chk("ltof", i, f_from_long((s32)t[5]), t[6]);
        chk("int", i, f_int(t[0]), t[7]);
    }
    f_format(0x40490FDBUL, buf);
    if (strcmp(buf, "3.14159")) { printf("format %s\n", buf); bad++; }
    printf(bad ? "sfp BAD %d\n" : "sfp ok\n", bad);
    if (argc > 1) {                                  /* sfptest b: the speed */
        u16 t0, t1, n;
        f32 x = 0x40490FDBUL, y = 0x3F9E0652UL, z = 0;
        t0 = mk_ticks(); while (mk_ticks() == t0) ; t0 = mk_ticks();
        for (n = 0; n < 500; n++) z = f_add(x, y);
        t1 = mk_ticks(); printf("add %u us\n", (u16)((t1 - t0) * 40));
        t0 = mk_ticks();
        for (n = 0; n < 500; n++) z = f_mul(x, y);
        t1 = mk_ticks(); printf("mul %u us\n", (u16)((t1 - t0) * 40));
        t0 = mk_ticks();
        for (n = 0; n < 100; n++) z = f_div(x, y);
        t1 = mk_ticks(); printf("div %u us\n", (u16)((t1 - t0) * 200));
        t0 = mk_ticks();
        for (n = 0; n < 100; n++) { f_parse("0.0458", &z); }
        t1 = mk_ticks(); printf("parse %u us\n", (u16)((t1 - t0) * 200));
        t0 = mk_ticks();
        for (n = 0; n < 500; n++) z = f_from_long(12345L);
        t1 = mk_ticks(); printf("ltof %u us\n", (u16)((t1 - t0) * 40));
        (void)z;
    }
    return bad != 0;
}
