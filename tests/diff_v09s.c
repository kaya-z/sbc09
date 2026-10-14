/* SPDX-License-Identifier: MIT */
/*
 * diff_v09s.c - differential test: m6809.c core vs. the sbc09 v09s.c simulator.
 *
 * v09s.c (sbc09, GPL v2) is included verbatim as the instruction-semantics
 * reference.  For every opcode (all three pages) random register / memory
 * contents are generated, one instruction is executed by both
 * implementations and registers, 64K of memory and the cycle count are
 * compared.
 *
 *   build:  make test-diff   (V09S_SRC=path/to/v09s.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define main v09s_main
#include V09S_SRC
#undef main
#include "../src/m6809.h"

static uint8_t mymem[65536];
static uint8_t rd(void *ctx, uint16_t a) { (void)ctx; return mymem[a]; }
static void wr(void *ctx, uint16_t a, uint8_t v) { (void)ctx; mymem[a] = v; }

static unsigned long rnd(void) { return (unsigned long)random(); }

static int cmp_cycles = 1;

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 200;
    long fails = 0, total = 0, skipped = 0;
    m6809_t cpu;
    static uint8_t snap[65536];
    srandom(12345);
    m6809_init(&cpu, NULL, rd, wr);

    for (int pfx = 0; pfx < 3; pfx++) {
        for (int op = 0; op < 256; op++) {
            int pre = pfx == 0 ? 0 : (pfx == 1 ? 0x10 : 0x11);
            /* instructions with host side effects in v09s */
            if (!pre && (op == 0x13 || op == 0x3c || op == 0x10 || op == 0x11)) continue;
            if (pre && op == 0x3f) continue;
            /* known v09s deviations from the MC6809 datasheet (see README):
             *  36/37 PSHU/PULU bit 6 uses U instead of S,
             *  3B    RTI tests the E bit of the *old* CC instead of the pulled one. */
            if (!pre && (op == 0x36 || op == 0x37 || op == 0x3b)) continue;
            /*  19    DAA in v09s ignores N/Z and uses a simplified correction (tested separately),
             *  2C-2F BGE/BLT/BGT/BLE: v09s computes N xor V from bits 3 and 1 without aligning them. */
            if (!pre && op == 0x19) continue;
            /*  3F    SWI: v09s stacks CC before setting the E flag (RTI could not restore it). */
            if (!pre && op == 0x3f) continue;
            if (op >= 0x2c && op <= 0x2f) continue;
            if (pre && (op == 0x10 || op == 0x11)) continue;
            int opfail = 0, opskip = 0;
            if (getenv("ONLY_OP") && strtol(getenv("ONLY_OP"), 0, 16) != op + (pre << 8)) continue;
            for (int it = 0; it < iters; it++) {
                /* random state */
                for (int i = 0; i < 65536; i++) mem[i] = (uint8_t)rnd();
                uint16_t pc = 0x2000 + (rnd() & 0x1fff);
                int n = 0;
                if (pre) mem[(uint16_t)(pc + n++)] = (uint8_t)pre;
                mem[(uint16_t)(pc + n++)] = (uint8_t)op;
                {   /* force a legal indexed postbyte */
                    int idx = (!pre && op >= 0x30 && op <= 0x33) || ((op >> 4) == 6 && !pre) ||
                              (op >= 0x80 && ((op & 0xf0) == 0xa0 || (op & 0xf0) == 0xe0));
                    if (idx) {
                        uint8_t *pb = &mem[(uint16_t)(pc + n)];
                        for (;;) {
                            uint8_t p = *pb;
                            int lo = p & 15, ind = p & 0x10;
                            int ok = !(p & 0x80) ||
                                (lo == 0x1 || lo == 0x3 || lo == 0x4 || lo == 0x5 || lo == 0x6 || lo == 0x8 ||
                                 lo == 0x9 || lo == 0xb || lo == 0xc || lo == 0xd) ||
                                ((lo == 0 || lo == 2) && !ind) || (lo == 0xf && ind);
                            if (ok) break;
                            *pb = (uint8_t)rnd();
                        }
                    }
                }
                if (!pre && (op == 0x1e || op == 0x1f)) {   /* same-size, defined registers only */
                    uint8_t *pb = &mem[(uint16_t)(pc + n)];
                    if (rnd() & 1) *pb = (uint8_t)(((rnd() % 6) << 4) | (rnd() % 6));
                    else           *pb = (uint8_t)(0x88 + ((rnd() % 4) << 4) + (rnd() % 4) - 8 + 8 - 8);
                    if (!(*pb & 0x80)) ; else *pb = (uint8_t)((8 + rnd() % 4) << 4 | (8 + rnd() % 4));
                }
                xreg = rnd(); yreg = rnd(); ureg = rnd();
                sreg = 0x6000 + (rnd() & 0x1fff);
                if (rnd() & 1) { xreg = 0x4000 + (rnd() & 0x1fff); }
                if (rnd() & 1) { yreg = 0x4000 + (rnd() & 0x1fff); }
                if (rnd() & 1) { ureg = 0x4000 + (rnd() & 0x1fff); }
                *areg = rnd(); *breg = rnd();
                dpreg = (rnd() & 1) ? 0x40 + (rnd() & 0x1f) : (uint8_t)rnd();
                ccreg = (uint8_t)rnd() & 0xef;     /* keep E.. ok, but I irrelevant */
                pcreg = pc; iflag = 0;
                /* constrain TFR/EXG random post byte validity left random on purpose */

                uint16_t i_x = xreg, i_y = yreg, i_u = ureg, i_s = sreg; uint8_t i_a = *areg, i_b = *breg, i_cc = ccreg, i_dp = dpreg;
                memcpy(mymem, mem, 65536);
                cpu.x = xreg; cpu.y = yreg; cpu.u = ureg; cpu.s = sreg;
                cpu.dd.r.a = *areg; cpu.dd.r.b = *breg; cpu.dp = dpreg;
                cpu.cc = ccreg; cpu.pc = pc;
                cpu.state = M6809_RUN; cpu.total_cycles = 0; cpu.illegal_count = 0;
                cpu.line[0] = cpu.line[1] = cpu.line[2] = 0; cpu.nmi_pending = 0;

                memcpy(snap, mem, 65536);
                /* run our core first: v09s executes *any* opcode after a prefix as if it were
                 * unprefixed (including SYNC/CWAI, which stop the host program), so only
                 * combinations that are defined on the real chip are handed to it. */
                int c_my = m6809_step(&cpu);
                total++;
                if (cpu.illegal_count) { opskip = 1; skipped++; continue; }
                ireg = mem[pcreg++]; cycles = 0;
                (*instrtable[ireg])();
                int c_ref = cycles;

                int bad = 0; char why[256]; why[0] = 0;
#define CK(name, a, b) if ((a) != (b)) { bad = 1; sprintf(why + strlen(why), " %s ref=%04x my=%04x", name, (unsigned)(a), (unsigned)(b)); }
                CK("A", *areg, cpu.dd.r.a) CK("B", *breg, cpu.dd.r.b)
                CK("X", xreg, cpu.x) CK("Y", yreg, cpu.y) CK("U", ureg, cpu.u)
                CK("S", sreg, cpu.s) CK("PC", pcreg, cpu.pc) CK("DP", dpreg, cpu.dp)
                {   /* H is undefined for ASR/ASL: do not compare */
                    uint8_t hm = 0xff;
                    if (!pre && (op == 0x47 || op == 0x48 || op == 0x57 || op == 0x58 ||
                                 op == 0x07 || op == 0x08 || op == 0x67 || op == 0x68 || op == 0x77 || op == 0x78)) hm = ~CC_H;
                    CK("CC", ccreg & hm, cpu.cc & hm)
                }
                if (pre == 0x10 && op >= 0x21 && op <= 0x2f && c_ref == c_my + 1) c_ref = c_my;   /* v09s: LBcc always 6 */
                if (!pre && op == 0x1f) c_ref -= 1;         /* v09s charges 7 for TFR, datasheet: 6 */
                if (!pre && op == 0x3f) c_ref = c_my;       /* v09s does not count SWI cycles      */
                if (cmp_cycles) {
                    /* v09s charges 1 cycle more than the datasheet for [nnnn] */
                    int idxop = (!pre && op >= 0x30 && op <= 0x33) || ((op >> 4) == 6 && !pre) ||
                                (op >= 0x80 && ((op & 0xf0) == 0xa0 || (op & 0xf0) == 0xe0));
                    int extind = idxop && ((snap[(uint16_t)(pc + n)] & 0x9f) == 0x9f);
                    if (extind && c_ref == c_my + 1) c_ref = c_my;
                    CK("cyc", c_ref, c_my)
                }
                if (memcmp(mem, mymem, 65536)) {
                    bad = 1;
                    for (int i = 0; i < 65536; i++) if (mem[i] != mymem[i]) {
                        sprintf(why + strlen(why), " mem[%04x] ref=%02x my=%02x", i, mem[i], mymem[i]); break; }
                }
                if (bad) {
                    opfail++; fails++;
                    if (opfail <= 2) {
                        printf("MISMATCH %s%02x:", pre ? (pre == 0x10 ? "10" : "11") : "  ", op);
                        printf(" bytes=");
                        for (int i = 0; i < 6; i++) printf("%02x", snap[(uint16_t)(pc + i)]);
                        printf(" cc0=%02x%s\n", 0, why);
                        printf("   init x=%04x y=%04x u=%04x s=%04x a=%02x b=%02x cc=%02x dp=%02x\n", i_x, i_y, i_u, i_s, i_a, i_b, i_cc, i_dp);
                    }
                }
            }
            if (opfail) printf("  -> %s%02x: %d/%d mismatching\n", pre ? (pre == 0x10 ? "10" : "11") : "  ", op, opfail, iters);
            (void)opskip;
        }
    }
    printf("total=%ld mismatches=%ld illegal-skipped=%ld\n", total, fails, skipped);
    return fails != 0;
}
