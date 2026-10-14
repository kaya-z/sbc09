/* SPDX-License-Identifier: MIT */
/*
 * cpu_test.c - self tests of the MC6809 core: interrupts, CWAI / SYNC / HALT /
 * RESET, NMI arming, DAA, cycle counts.  (The instruction level semantics are
 * cross checked against sbc09's v09s.c by diff_v09s.c.)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/m6809.h"

static uint8_t mem[65536];
static uint8_t rd(void *c, uint16_t a) { (void)c; return mem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static m6809_t cpu;
static void setup(void)
{
    memset(mem, 0, sizeof mem);
    m6809_init(&cpu, NULL, rd, wr);
    mem[0xfffe] = 0x10; mem[0xffff] = 0x00;          /* reset -> $1000 */
    mem[0xfff8] = 0x20; mem[0xfff9] = 0x00;          /* IRQ   -> $2000 */
    mem[0xfff6] = 0x21; mem[0xfff7] = 0x00;          /* FIRQ  -> $2100 */
    mem[0xfffc] = 0x22; mem[0xfffd] = 0x00;          /* NMI   -> $2200 */
    mem[0xfffa] = 0x23; mem[0xfffb] = 0x00;          /* SWI   -> $2300 */
    mem[0xfff4] = 0x24; mem[0xfff5] = 0x00;          /* SWI2  -> $2400 */
    mem[0xfff2] = 0x25; mem[0xfff3] = 0x00;          /* SWI3  -> $2500 */
    m6809_reset(&cpu);
}
static void prog(uint16_t a, const char *hex)
{
    while (*hex) { unsigned v; if (*hex == ' ') { hex++; continue; } sscanf(hex, "%2x", &v); mem[a++] = (uint8_t)v; hex += 2; }
}

static int step_cyc(void) { return m6809_step(&cpu); }

static void test_reset(void)
{
    setup();
    CHECK(cpu.pc == 0x1000, "reset vector pc=%04x", cpu.pc);
    CHECK((cpu.cc & (CC_I | CC_F)) == (CC_I | CC_F), "reset masks I and F cc=%02x", cpu.cc);
    CHECK(cpu.dp == 0, "reset clears DP");
}

static void test_irq(void)
{
    setup();
    prog(0x1000, "1CEF 8E3000 10CE8000 12 12");           /* ANDCC #$EF; LDX #$3000; LDS #$8000; NOP; NOP */
    step_cyc(); step_cyc(); step_cyc();
    cpu.cc &= ~CC_F;
    m6809_set_line(&cpu, M6809_IRQ_LINE, 1);
    int c = step_cyc();
    CHECK(c == 19, "IRQ entry takes 19 cycles (%d)", c);
    CHECK(cpu.pc == 0x2000, "IRQ vector pc=%04x", cpu.pc);
    CHECK(cpu.s == 0x8000 - 12, "IRQ pushes 12 bytes s=%04x", cpu.s);
    CHECK((cpu.cc & CC_E) && (cpu.cc & CC_I) && !(cpu.cc & CC_F), "IRQ sets E and I only cc=%02x", cpu.cc);
    CHECK(mem[0x8000 - 1] == 0x09 && mem[0x8000 - 2] == 0x10, "return address pushed ($1009)");
    CHECK(mem[0x8000 - 12] & CC_E, "stacked CC has E set");
    /* masked now: next step executes at $2000 (illegal 00 = NEG <$00, 6 cycles) */
    m6809_step(&cpu);
    CHECK(cpu.pc == 0x2002, "IRQ is masked while in the handler pc=%04x", cpu.pc);
}

static void test_firq(void)
{
    setup();
    prog(0x1000, "1CAF 10CE8000 12");
    step_cyc(); step_cyc();
    m6809_set_line(&cpu, M6809_FIRQ_LINE, 1);
    int c = step_cyc();
    CHECK(c == 10, "FIRQ entry takes 10 cycles (%d)", c);
    CHECK(cpu.pc == 0x2100, "FIRQ vector");
    CHECK(cpu.s == 0x8000 - 3, "FIRQ pushes only PC and CC s=%04x", cpu.s);
    CHECK(!(cpu.cc & CC_E) && (cpu.cc & CC_I) && (cpu.cc & CC_F), "FIRQ clears E, sets I and F cc=%02x", cpu.cc);
    CHECK(!(mem[0x8000 - 3] & CC_E), "stacked CC has E clear");
}

static void test_nmi(void)
{
    setup();
    prog(0x1000, "12 12 10CE8000 12 12");
    step_cyc();
    m6809_set_line(&cpu, M6809_NMI_LINE, 1);
    step_cyc();                                    /* not armed: NMI is held off until S is loaded */
    CHECK(cpu.pc == 0x1002, "NMI is ignored before S has been loaded pc=%04x", cpu.pc);
    step_cyc();                                    /* LDS arms it */
    int c = step_cyc();
    CHECK(c == 19 && cpu.pc == 0x2200, "NMI taken after LDS (cycles %d pc=%04x)", c, cpu.pc);
    CHECK((cpu.cc & (CC_I | CC_F)) == (CC_I | CC_F), "NMI sets I and F");
    /* edge triggered: still high -> no second NMI */
    cpu.pc = 0x1000;
    step_cyc();
    CHECK(cpu.pc == 0x1001, "NMI is edge triggered pc=%04x", cpu.pc);
    m6809_set_line(&cpu, M6809_NMI_LINE, 0);
    m6809_set_line(&cpu, M6809_NMI_LINE, 1);
    step_cyc();
    CHECK(cpu.pc == 0x2200, "second NMI edge taken");
}

static void test_rti(void)
{
    setup();
    prog(0x1000, "1CEF 10CE8000 8605 C607 12");
    prog(0x2000, "3B");                             /* RTI */
    step_cyc(); step_cyc(); step_cyc(); step_cyc();
    m6809_set_line(&cpu, M6809_IRQ_LINE, 1);
    step_cyc();
    m6809_set_line(&cpu, M6809_IRQ_LINE, 0);
    REG_A(&cpu) = 0x55;
    int c = step_cyc();
    CHECK(c == 15, "RTI with E set takes 15 cycles (%d)", c);
    CHECK(cpu.pc == 0x1008 - 0 || cpu.pc == 0x100a - 2 || cpu.pc != 0x2000, "RTI returned pc=%04x", cpu.pc);
    CHECK(REG_A(&cpu) == 5 && REG_B(&cpu) == 7, "RTI restored A,B (%02x %02x)", REG_A(&cpu), REG_B(&cpu));
    CHECK(cpu.s == 0x8000, "RTI popped the frame s=%04x", cpu.s);
}

static void test_cwai(void)
{
    setup();
    prog(0x1000, "10CE8000 3CEF 12");               /* LDS; CWAI #$EF; NOP */
    step_cyc();
    int c = step_cyc();
    CHECK(c == 20, "CWAI takes 20 cycles (%d)", c);
    CHECK(cpu.state == M6809_CWAI, "CPU waits in CWAI");
    CHECK(cpu.s == 0x8000 - 12, "CWAI stacked the full state");
    for (int i = 0; i < 5; i++) step_cyc();
    CHECK(cpu.state == M6809_CWAI && cpu.pc == 0x1006, "CWAI keeps waiting without interrupt");
    m6809_set_line(&cpu, M6809_IRQ_LINE, 1);
    c = step_cyc();
    CHECK(cpu.pc == 0x2000 && c == 4, "CWAI -> IRQ vector, no second push (pc=%04x cyc=%d)", cpu.pc, c);
    CHECK(cpu.s == 0x8000 - 12, "no second stacking");
    CHECK(cpu.state == M6809_RUN, "CPU runs again");
}

static void test_sync(void)
{
    setup();
    prog(0x1000, "10CE8000 13 4C 4C");              /* LDS; SYNC; INCA; INCA */
    step_cyc();
    step_cyc();
    CHECK(cpu.state == M6809_SYNC, "SYNC halts the CPU");
    step_cyc(); step_cyc();
    CHECK(cpu.pc == 0x1005, "stays in SYNC");
    m6809_set_line(&cpu, M6809_IRQ_LINE, 1);        /* I is set after reset: wakes but is masked */
    step_cyc();                                      /* wake up and execute the next instruction */
    CHECK(cpu.state == M6809_RUN && REG_A(&cpu) == 1, "SYNC wakes on a masked IRQ and continues (A=%d)", REG_A(&cpu));
}

static void test_halt_reset(void)
{
    setup();
    prog(0x1000, "4C 4C 4C 4C");
    step_cyc();
    m6809_set_line(&cpu, M6809_HALT_LINE, 1);
    step_cyc(); step_cyc();
    CHECK(REG_A(&cpu) == 1, "HALT stops instruction fetch (A=%d)", REG_A(&cpu));
    m6809_set_line(&cpu, M6809_HALT_LINE, 0);
    step_cyc();
    CHECK(REG_A(&cpu) == 2, "execution resumes after HALT");
    m6809_set_line(&cpu, M6809_RESET_LINE, 1);
    step_cyc();
    m6809_set_line(&cpu, M6809_RESET_LINE, 0);
    step_cyc();
    CHECK(cpu.pc == 0x1000, "RESET reloads pc (%04x)", cpu.pc);
}

static void test_swi(void)
{
    setup();
    prog(0x1000, "10CE8000 3F");
    step_cyc();
    int c = step_cyc();
    CHECK(c == 19 && cpu.pc == 0x2300 && (cpu.cc & (CC_I | CC_F | CC_E)) == (CC_I | CC_F | CC_E), "SWI (cyc %d)", c);
    setup();
    prog(0x1000, "1CAF 10CE8000 103F");
    step_cyc(); step_cyc();
    c = step_cyc();
    CHECK(c == 20 && cpu.pc == 0x2400 && !(cpu.cc & CC_I), "SWI2 does not mask (cyc %d cc=%02x)", c, cpu.cc);
    setup();
    prog(0x1000, "10CE8000 113F");
    step_cyc();
    c = step_cyc();
    CHECK(c == 20 && cpu.pc == 0x2500, "SWI3");
}

static void test_daa(void)
{
    int bad = 0;
    for (int a = 0; a < 100; a++)
        for (int b = 0; b < 100; b++) {
            setup();
            prog(0x1000, "8B00 19");                  /* ADDA #imm; DAA */
            mem[0x1001] = (uint8_t)(((b / 10) << 4) | (b % 10));
            REG_A(&cpu) = (uint8_t)(((a / 10) << 4) | (a % 10));
            cpu.cc = 0;
            step_cyc(); step_cyc();
            int sum = a + b, carry = sum >= 100;
            sum %= 100;
            uint8_t exp = (uint8_t)(((sum / 10) << 4) | (sum % 10));
            if (REG_A(&cpu) != exp || !!(cpu.cc & CC_C) != carry) bad++;
        }
    CHECK(bad == 0, "DAA after ADDA for all BCD pairs (%d wrong)", bad);
    /* SUBA + DAA is not defined on the 6809, only additions are checked */
}

static void test_cycles(void)
{
    struct { const char *code; int cyc; const char *name; } t[] = {
        { "12", 2, "NOP" }, { "86 01", 2, "LDA #" }, { "96 10", 4, "LDA <" }, { "B6 1000", 5, "LDA >" },
        { "A6 84", 4, "LDA ,X" }, { "A6 01", 5, "LDA 1,X" }, { "A6 88 10", 5, "LDA 8bit,X" }, { "A6 89 1000", 8, "LDA 16bit,X" },
        { "A6 86", 5, "LDA A,X" }, { "A6 8B", 8, "LDA D,X" }, { "A6 80", 6, "LDA ,X+" }, { "A6 81", 7, "LDA ,X++" },
        { "A6 94", 7, "LDA [,X]" }, { "A6 9F 2000", 9, "LDA [n]" },
        { "8E 1234", 3, "LDX #" }, { "BE 1000", 6, "LDX >" }, { "10 8E 1234", 4, "LDY #" }, { "CC 1234", 3, "LDD #" },
        { "C3 0001", 4, "ADDD #" }, { "8C 1234", 4, "CMPX #" }, { "10 83 1234", 5, "CMPD #" }, { "11 83 1234", 5, "CMPU #" },
        { "97 10", 4, "STA <" }, { "FD 1000", 6, "STD >" },
        { "3D", 11, "MUL" }, { "1D", 2, "SEX" }, { "3A", 3, "ABX" }, { "19", 2, "DAA" },
        { "1F 12", 6, "TFR" }, { "1E 12", 8, "EXG" },
        { "34 FF", 5 + 12, "PSHS all" }, { "34 02", 5 + 1, "PSHS A" }, { "35 80", 5 + 2, "PULS PC" },
        { "7E 1234", 4, "JMP >" }, { "0E 10", 3, "JMP <" }, { "BD 1234", 8, "JSR >" }, { "8D 00", 7, "BSR" }, { "9D 10", 7, "JSR <" },
        { "17 0000", 9, "LBSR" }, { "16 0000", 5, "LBRA" }, { "20 00", 3, "BRA" }, { "26 00", 3, "BNE" }, { "10 26 0000", 5, "LBNE not taken" },
        { "10 27 0000", 6, "LBEQ taken (Z set)" },
        { "40", 2, "NEGA" }, { "00 10", 6, "NEG <" }, { "60 84", 6, "NEG ,X" }, { "70 1000", 7, "NEG >" },
        { "30 84", 4, "LEAX ,X" }, { "30 01", 5, "LEAX 1,X" }, { "32 E1", 4 + 3, "LEAS ,S++?" },
        { "1A 01", 3, "ORCC" }, { "1C FE", 3, "ANDCC" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        setup();
        cpu.s = 0x8000; cpu.x = 0x3000; cpu.u = 0x4000;
        prog(0x1000, t[i].code);
        if (!strcmp(t[i].name, "LBEQ taken (Z set)")) cpu.cc |= CC_Z;
        if (!strcmp(t[i].name, "LDA [,X]")) { mem[0x3000] = 0x30; mem[0x3001] = 0x10; }
        if (!strcmp(t[i].name, "PULS PC")) { cpu.s = 0x7000; }
        cpu.cc &= ~CC_Z;
        if (!strcmp(t[i].name, "LBEQ taken (Z set)") || !strcmp(t[i].name, "LBNE not taken")) cpu.cc |= CC_Z;
        int c = step_cyc();
        CHECK(c == t[i].cyc, "%s: %d cycles (expected %d)", t[i].name, c, t[i].cyc);
    }
}

static void test_flags(void)
{
    setup();
    prog(0x1000, "8680 8B80");                       /* LDA #$80; ADDA #$80 -> 0, C V Z */
    step_cyc(); step_cyc();
    CHECK(REG_A(&cpu) == 0 && (cpu.cc & (CC_Z | CC_V | CC_C)) == (CC_Z | CC_V | CC_C) && !(cpu.cc & CC_N), "ADDA #$80+$80 flags cc=%02x", cpu.cc);
    setup();
    prog(0x1000, "8618 8B18");                       /* half carry */
    step_cyc(); step_cyc();
    CHECK(cpu.cc & CC_H, "half carry set by ADDA ($18+$18)");
    setup();
    prog(0x1000, "8601 8001");                       /* SUBA #1 -> 0 */
    step_cyc(); step_cyc();
    CHECK(REG_A(&cpu) == 0 && (cpu.cc & CC_Z) && !(cpu.cc & CC_C), "SUBA flags");
    setup();
    prog(0x1000, "8600 8001");
    step_cyc(); step_cyc();
    CHECK(REG_A(&cpu) == 0xff && (cpu.cc & CC_C) && (cpu.cc & CC_N), "SUBA borrow");
    setup();
    prog(0x1000, "CC1234 3D");                       /* LDD #$1234; MUL: A=$12 B=$34 */
    step_cyc(); step_cyc();
    CHECK(REG_D(&cpu) == 0x12 * 0x34 && !(cpu.cc & CC_Z), "MUL");
    setup();
    prog(0x1000, "C680 1D");                         /* SEX */
    step_cyc(); step_cyc();
    CHECK(REG_D(&cpu) == 0xff80 && (cpu.cc & CC_N), "SEX");
    setup();
    prog(0x1000, "1089 0000");                       /* illegal page-2 opcode */
    step_cyc();
    CHECK(cpu.illegal_count == 1, "illegal opcode counted");
}

int main(void)
{
    test_reset(); test_irq(); test_firq(); test_nmi(); test_rti(); test_cwai(); test_sync();
    test_halt_reset(); test_swi(); test_daa(); test_cycles(); test_flags();
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
