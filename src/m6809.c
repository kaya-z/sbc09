/* SPDX-License-Identifier: MIT */
/*
 * m6809.c - Motorola MC6809 CPU core
 *
 * See m6809.h for credits.  In short:
 *   - opcode semantics / flag behaviour / cycle counts: sbc09 v09s.c
 *   - clock, cycle budget and CPU state-machine concepts: MAME m6809.cpp
 *
 * Timing model: every instruction is executed atomically and charged its
 * documented MC6809 cycle count (base count + indexed postbyte surcharge,
 * one extra cycle for the $10/$11 page prefix, 19/10 cycles for interrupt
 * entry, ...), see the MC6809E datasheet / programming manual.  `icount` is the budget the
 * scheduler hands to m6809_run() and `total_cycles` is the absolute clock
 * that peripherals (timer, UART pacing) use.
 */
#include <string.h>
#include "m6809.h"

#define RD(a)    (c->read(c->ctx, (uint16_t)(a)))
#define WR(a, v) (c->write(c->ctx, (uint16_t)(a), (uint8_t)(v)))

static inline uint8_t fetch8(m6809_t *c) { return RD(c->pc++); }
static inline uint16_t fetch16(m6809_t *c)
{
    uint16_t h = fetch8(c);
    return (uint16_t)((h << 8) | fetch8(c));
}
static inline uint16_t rd16(m6809_t *c, uint16_t a)
{
    uint16_t h = RD(a);
    return (uint16_t)((h << 8) | RD((uint16_t)(a + 1)));
}
static inline void wr16(m6809_t *c, uint16_t a, uint16_t v)
{
    WR(a, v >> 8);
    WR((uint16_t)(a + 1), v);
}

/* ------------------------------------------------------------------ */
/* stacks                                                              */
/* ------------------------------------------------------------------ */
static inline void push8(m6809_t *c, uint16_t *sp, uint8_t v)
{
    --*sp;
    WR(*sp, v);
}
static inline void push16(m6809_t *c, uint16_t *sp, uint16_t v)
{
    push8(c, sp, (uint8_t)v);
    push8(c, sp, (uint8_t)(v >> 8));
}
static inline uint8_t pull8(m6809_t *c, uint16_t *sp)
{
    uint8_t v = RD(*sp);
    ++*sp;
    return v;
}
static inline uint16_t pull16(m6809_t *c, uint16_t *sp)
{
    uint16_t h = pull8(c, sp);
    return (uint16_t)((h << 8) | pull8(c, sp));
}

/* ------------------------------------------------------------------ */
/* flags                                                               */
/* ------------------------------------------------------------------ */
static inline void set_nz8(m6809_t *c, uint8_t v)
{
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | ((v & 0x80) ? CC_N : 0) |
                      (v == 0 ? CC_Z : 0));
}
static inline void set_nz16(m6809_t *c, uint16_t v)
{
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | ((v & 0x8000) ? CC_N : 0) |
                      (v == 0 ? CC_Z : 0));
}
static inline void flags_logic8(m6809_t *c, uint8_t v)
{
    set_nz8(c, v);
    c->cc &= ~CC_V;
}
static inline void flags_logic16(m6809_t *c, uint16_t v)
{
    set_nz16(c, v);
    c->cc &= ~CC_V;
}

static uint8_t add8(m6809_t *c, uint8_t a, uint8_t b, unsigned cin)
{
    unsigned r = a + b + cin;
    c->cc &= ~(CC_H | CC_N | CC_Z | CC_V | CC_C);
    if ((a ^ b ^ r) & 0x10) c->cc |= CC_H;
    if (r & 0x80) c->cc |= CC_N;
    if ((r & 0xff) == 0) c->cc |= CC_Z;
    if (~(a ^ b) & (a ^ r) & 0x80) c->cc |= CC_V;
    if (r & 0x100) c->cc |= CC_C;
    return (uint8_t)r;
}

static uint8_t sub8(m6809_t *c, uint8_t a, uint8_t b, unsigned cin)
{
    unsigned r = (unsigned)a - b - cin;
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x80) c->cc |= CC_N;
    if ((r & 0xff) == 0) c->cc |= CC_Z;
    if ((a ^ b) & (a ^ r) & 0x80) c->cc |= CC_V;
    if (r & 0x100) c->cc |= CC_C;
    return (uint8_t)r;
}

static uint16_t add16(m6809_t *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x8000) c->cc |= CC_N;
    if ((r & 0xffff) == 0) c->cc |= CC_Z;
    if (~(a ^ b) & (a ^ r) & 0x8000) c->cc |= CC_V;
    if (r & 0x10000) c->cc |= CC_C;
    return (uint16_t)r;
}

static uint16_t sub16(m6809_t *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (r & 0x8000) c->cc |= CC_N;
    if ((r & 0xffff) == 0) c->cc |= CC_Z;
    if ((a ^ b) & (a ^ r) & 0x8000) c->cc |= CC_V;
    if (r & 0x10000) c->cc |= CC_C;
    return (uint16_t)r;
}

/* single operand ops; kind = low nibble of the opcode */
static uint8_t unary(m6809_t *c, int kind, uint8_t v)
{
    unsigned r;
    switch (kind) {
    case 0x0: /* NEG */
        r = (unsigned)(0 - v);
        c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
        if (r & 0x80) c->cc |= CC_N;
        if ((r & 0xff) == 0) c->cc |= CC_Z;
        if (v == 0x80) c->cc |= CC_V;
        if (v != 0) c->cc |= CC_C;
        return (uint8_t)r;
    case 0x3: /* COM */
        v = (uint8_t)~v;
        set_nz8(c, v);
        c->cc = (uint8_t)((c->cc & ~CC_V) | CC_C);
        return v;
    case 0x4: /* LSR */
        c->cc = (uint8_t)((c->cc & ~(CC_N | CC_C)) | (v & 1));
        v >>= 1;
        c->cc = (uint8_t)((c->cc & ~CC_Z) | (v == 0 ? CC_Z : 0));
        return v;
    case 0x6: { /* ROR */
        uint8_t cin = (c->cc & CC_C) ? 0x80 : 0;
        c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));
        v = (uint8_t)((v >> 1) | cin);
        set_nz8(c, v);
        return v;
    }
    case 0x7: /* ASR */
        c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));
        v = (uint8_t)((v >> 1) | (v & 0x80));
        set_nz8(c, v);
        return v;
    case 0x8: /* ASL / LSL */
        r = (unsigned)v << 1;
        c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
        if (r & 0x80) c->cc |= CC_N;
        if ((r & 0xff) == 0) c->cc |= CC_Z;
        if ((v ^ r) & 0x80) c->cc |= CC_V;
        if (r & 0x100) c->cc |= CC_C;
        return (uint8_t)r;
    case 0x9: { /* ROL */
        unsigned cin = (c->cc & CC_C) ? 1 : 0;
        r = ((unsigned)v << 1) | cin;
        c->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
        if (r & 0x80) c->cc |= CC_N;
        if ((r & 0xff) == 0) c->cc |= CC_Z;
        if ((v ^ r) & 0x80) c->cc |= CC_V;
        if (r & 0x100) c->cc |= CC_C;
        return (uint8_t)r;
    }
    case 0xA: /* DEC */
        r = (unsigned)(v - 1);
        c->cc &= ~(CC_N | CC_Z | CC_V);
        if (r & 0x80) c->cc |= CC_N;
        if ((r & 0xff) == 0) c->cc |= CC_Z;
        if (v == 0x80) c->cc |= CC_V;
        return (uint8_t)r;
    case 0xC: /* INC */
        r = (unsigned)(v + 1);
        c->cc &= ~(CC_N | CC_Z | CC_V);
        if (r & 0x80) c->cc |= CC_N;
        if ((r & 0xff) == 0) c->cc |= CC_Z;
        if (v == 0x7f) c->cc |= CC_V;
        return (uint8_t)r;
    case 0xD: /* TST */
        flags_logic8(c, v);
        return v;
    case 0xF: /* CLR */
        c->cc = (uint8_t)((c->cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
        return 0;
    }
    return v;
}

static int is_unary(int kind)
{
    switch (kind) {
    case 0x0: case 0x3: case 0x4: case 0x6: case 0x7: case 0x8:
    case 0x9: case 0xA: case 0xC: case 0xD: case 0xF:
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* addressing modes                                                    */
/* ------------------------------------------------------------------ */
static uint16_t *index_reg(m6809_t *c, uint8_t post)
{
    switch ((post >> 5) & 3) {
    case 0: return &c->x;
    case 1: return &c->y;
    case 2: return &c->u;
    default: return &c->s;
    }
}

/* indexed effective address; adds the postbyte surcharge to c->cyc */
static uint16_t ea_indexed(m6809_t *c)
{
    uint8_t post = fetch8(c);
    uint16_t *r = index_reg(c, post);
    uint16_t ea;
    int ind;

    if (!(post & 0x80)) {                 /* 5 bit signed offset */
        int off = post & 0x1f;
        if (off & 0x10) off -= 0x20;
        c->cyc += 1;
        return (uint16_t)(*r + off);
    }
    ind = post & 0x10;
    switch (post & 0x0f) {
    case 0x0: ea = *r; *r += 1; c->cyc += 2; ind = 0; break;
    case 0x1: ea = *r; *r += 2; c->cyc += 3; break;
    case 0x2: *r -= 1; ea = *r; c->cyc += 2; ind = 0; break;
    case 0x3: *r -= 2; ea = *r; c->cyc += 3; break;
    case 0x4: ea = *r; break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)REG_B(c)); c->cyc += 1; break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)REG_A(c)); c->cyc += 1; break;
    case 0x8: { int8_t o = (int8_t)fetch8(c); ea = (uint16_t)(*r + o); c->cyc += 1; break; }
    case 0x9: { uint16_t o = fetch16(c); ea = (uint16_t)(*r + o); c->cyc += 4; break; }
    case 0xB: ea = (uint16_t)(*r + REG_D(c)); c->cyc += 4; break;
    case 0xC: { int8_t o = (int8_t)fetch8(c); ea = (uint16_t)(c->pc + o); c->cyc += 1; break; }
    case 0xD: { uint16_t o = fetch16(c); ea = (uint16_t)(c->pc + o); c->cyc += 5; break; }
    case 0xF:
        if (post & 0x10) { ea = fetch16(c); c->cyc += 5; ind = 0; /* handled below */
            return rd16(c, ea); }
        ea = 0; break;                     /* illegal */
    default:                               /* 0x7, 0xA, 0xE illegal */
        ea = 0; ind = 0; break;
    }
    if (ind) {                             /* indirect: +3 over the direct form */
        c->cyc += 3;
        ea = rd16(c, ea);
    }
    return ea;
}

enum { AM_IMM = 0, AM_DIR = 1, AM_IDX = 2, AM_EXT = 3 };

/* effective address for mode m; size = operand size for immediate */
static uint16_t ea_mode(m6809_t *c, int m, int size)
{
    uint16_t ea;
    switch (m) {
    case AM_IMM: ea = c->pc; c->pc = (uint16_t)(c->pc + size); return ea;
    case AM_DIR: return (uint16_t)((c->dp << 8) | fetch8(c));
    case AM_IDX: return ea_indexed(c);
    default:     return fetch16(c);
    }
}

/* ------------------------------------------------------------------ */
/* TFR / EXG                                                           */
/* ------------------------------------------------------------------ */
static uint16_t get_reg(m6809_t *c, int r)
{
    switch (r) {
    case 0: return REG_D(c);
    case 1: return c->x;
    case 2: return c->y;
    case 3: return c->u;
    case 4: return c->s;
    case 5: return c->pc;
    case 8: return (uint16_t)(REG_A(c) | 0xff00);
    case 9: return (uint16_t)(REG_B(c) | 0xff00);
    case 10: return (uint16_t)(c->cc | (c->cc << 8));
    case 11: return (uint16_t)(c->dp | (c->dp << 8));
    default: return 0xffff;
    }
}

static void set_reg(m6809_t *c, int r, uint16_t v)
{
    switch (r) {
    case 0: REG_D(c) = v; break;
    case 1: c->x = v; break;
    case 2: c->y = v; break;
    case 3: c->u = v; break;
    case 4: c->s = v; c->nmi_armed = 1; break;
    case 5: c->pc = v; break;
    case 8: REG_A(c) = (uint8_t)v; break;
    case 9: REG_B(c) = (uint8_t)v; break;
    case 10: c->cc = (uint8_t)v; break;
    case 11: c->dp = (uint8_t)v; break;
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/* push / pull register sets                                           */
/* ------------------------------------------------------------------ */
static void push_all(m6809_t *c)
{
    push16(c, &c->s, c->pc);
    push16(c, &c->s, c->u);
    push16(c, &c->s, c->y);
    push16(c, &c->s, c->x);
    push8(c, &c->s, c->dp);
    push8(c, &c->s, REG_B(c));
    push8(c, &c->s, REG_A(c));
    push8(c, &c->s, c->cc);
}

static void psh(m6809_t *c, uint16_t *sp, uint16_t other, uint8_t m)
{
    int n = 0;
    if (m & 0x80) { push16(c, sp, c->pc); n += 2; }
    if (m & 0x40) { push16(c, sp, other); n += 2; }
    if (m & 0x20) { push16(c, sp, c->y); n += 2; }
    if (m & 0x10) { push16(c, sp, c->x); n += 2; }
    if (m & 0x08) { push8(c, sp, c->dp); n++; }
    if (m & 0x04) { push8(c, sp, REG_B(c)); n++; }
    if (m & 0x02) { push8(c, sp, REG_A(c)); n++; }
    if (m & 0x01) { push8(c, sp, c->cc); n++; }
    c->cyc += n;
}

static void pul(m6809_t *c, uint16_t *sp, uint16_t *other, uint8_t m)
{
    int n = 0;
    if (m & 0x01) { c->cc = pull8(c, sp); n++; }
    if (m & 0x02) { REG_A(c) = pull8(c, sp); n++; }
    if (m & 0x04) { REG_B(c) = pull8(c, sp); n++; }
    if (m & 0x08) { c->dp = pull8(c, sp); n++; }
    if (m & 0x10) { c->x = pull16(c, sp); n += 2; }
    if (m & 0x20) { c->y = pull16(c, sp); n += 2; }
    if (m & 0x40) { *other = pull16(c, sp); n += 2; }
    if (m & 0x80) { c->pc = pull16(c, sp); n += 2; }
    c->cyc += n;
}

/* ------------------------------------------------------------------ */
/* lines / reset                                                       */
/* ------------------------------------------------------------------ */
void m6809_init(m6809_t *c, void *ctx, m6809_read_fn rd, m6809_write_fn wr)
{
    memset(c, 0, sizeof *c);
    c->ctx = ctx;
    c->read = rd;
    c->write = wr;
    c->cc = CC_I | CC_F;
}

void m6809_reset(m6809_t *c)
{
    c->cc |= CC_I | CC_F;
    c->dp = 0;
    c->state = M6809_RUN;
    c->nmi_pending = 0;
    c->nmi_armed = 0;
    c->cwai_ready = 0;
    c->lic = 1;
    c->pc = rd16(c, VEC_RESET);
}

void m6809_set_line(m6809_t *c, int line, int level)
{
    level = level ? 1 : 0;
    if (line == M6809_NMI_LINE && level && !c->line[line])
        c->nmi_pending = 1;                    /* edge triggered */
    c->line[line] = (uint8_t)level;
}

/* ------------------------------------------------------------------ */
/* interrupt entry                                                     */
/* ------------------------------------------------------------------ */
static void do_irq_entry(m6809_t *c, int line)
{
    uint16_t vec;
    if (c->on_irq_ack) c->on_irq_ack(c, line);
    switch (line) {
    case M6809_NMI_LINE:
        c->nmi_pending = 0;
        vec = VEC_NMI;
        if (!c->cwai_ready) { c->cc |= CC_E; push_all(c); }
        c->cc |= CC_I | CC_F;
        c->cyc += c->cwai_ready ? 4 : 19;
        break;
    case M6809_FIRQ_LINE:
        vec = VEC_FIRQ;
        if (!c->cwai_ready) {
            c->cc &= ~CC_E;
            push16(c, &c->s, c->pc);
            push8(c, &c->s, c->cc);
        }
        c->cc |= CC_I | CC_F;
        c->cyc += c->cwai_ready ? 4 : 10;
        break;
    default: /* IRQ */
        vec = VEC_IRQ;
        if (!c->cwai_ready) { c->cc |= CC_E; push_all(c); }
        c->cc |= CC_I;
        c->cyc += c->cwai_ready ? 4 : 19;
        break;
    }
    c->pc = rd16(c, vec);
    if (c->cwai_ready) c->idle_count++;      /* the pass of the wait loop that saw the interrupt */
    c->cwai_ready = 0;
    c->state = M6809_RUN;
    c->irq_entered = 1;
}

static void do_swi(m6809_t *c, uint16_t vec, int mask)
{
    c->cc |= CC_E;
    push_all(c);
    if (mask) c->cc |= CC_I | CC_F;
    c->pc = rd16(c, vec);
}

/* ------------------------------------------------------------------ */
/* the interpreter                                                     */
/* ------------------------------------------------------------------ */
static void illegal(m6809_t *c, uint8_t prefix, uint8_t op)
{
    c->illegal_count++;
    c->illegal_pc = c->insn_pc;
    c->cyc += 1;
    if (c->on_illegal) c->on_illegal(c, c->insn_pc, prefix, op);
}

static inline int branch_cond(m6809_t *c, int cond)
{
    uint8_t cc = c->cc;
    int n = !!(cc & CC_N), z = !!(cc & CC_Z), v = !!(cc & CC_V), cy = !!(cc & CC_C);
    switch (cond) {
    case 0x0: return 1;                    /* BRA */
    case 0x1: return 0;                    /* BRN */
    case 0x2: return !(cy | z);            /* BHI */
    case 0x3: return cy | z;               /* BLS */
    case 0x4: return !cy;                  /* BCC */
    case 0x5: return cy;                   /* BCS */
    case 0x6: return !z;                   /* BNE */
    case 0x7: return z;                    /* BEQ */
    case 0x8: return !v;                   /* BVC */
    case 0x9: return v;                    /* BVS */
    case 0xA: return !n;                   /* BPL */
    case 0xB: return n;                    /* BMI */
    case 0xC: return !(n ^ v);             /* BGE */
    case 0xD: return n ^ v;                /* BLT */
    case 0xE: return !(z | (n ^ v));       /* BGT */
    default:  return z | (n ^ v);          /* BLE */
    }
}

/* base cycle counts for the ALU groups 0x80-0xFF, indexed by mode */
static const uint8_t cyc_b8[4]  = { 2, 4, 4, 5 };   /* 8 bit ops            */
static const uint8_t cyc_w1[4]  = { 3, 5, 5, 6 };   /* LDX LDD STX STD ...  */
static const uint8_t cyc_w2[4]  = { 4, 6, 6, 7 };   /* SUBD ADDD CMPX       */

/* Execute one ALU-group instruction (0x80..0xFF).  `pfx` selects page. */
static void exec_alu(m6809_t *c, uint8_t pfx, uint8_t op)
{
    int m = (op >> 4) & 3;
    int lo = op & 0x0f;
    int grpB = op & 0x40;
    uint16_t ea;
    uint8_t v8;
    uint16_t v16;

    /* ---- 16 bit / special column groups first ---- */
    if (!grpB) {
        switch (lo) {
        case 0x3: /* SUBD / CMPD / CMPU */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w2[m];
            v16 = rd16(c, ea);
            if (pfx == 0x11) { sub16(c, c->u, v16); }
            else if (pfx == 0x10) { sub16(c, REG_D(c), v16); }
            else REG_D(c) = sub16(c, REG_D(c), v16);
            return;
        case 0xC: /* CMPX / CMPY / CMPS */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w2[m];
            v16 = rd16(c, ea);
            sub16(c, pfx == 0x10 ? c->y : pfx == 0x11 ? c->s : c->x, v16);
            return;
        case 0xD: /* BSR / JSR */
            if (m == AM_IMM) {
                int8_t o = (int8_t)fetch8(c);
                push16(c, &c->s, c->pc);
                c->pc = (uint16_t)(c->pc + o);
                c->cyc += 7;
            } else {
                ea = ea_mode(c, m, 0);
                push16(c, &c->s, c->pc);
                c->pc = ea;
                c->cyc += (m == AM_EXT) ? 8 : 7;
            }
            return;
        case 0xE: /* LDX / LDY */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w1[m];
            v16 = rd16(c, ea);
            flags_logic16(c, v16);
            if (pfx == 0x10) c->y = v16; else c->x = v16;
            return;
        case 0xF: /* STX / STY */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w1[m];
            v16 = (pfx == 0x10) ? c->y : c->x;
            flags_logic16(c, v16);
            wr16(c, ea, v16);
            return;
        default: break;
        }
    } else {
        switch (lo) {
        case 0x3: /* ADDD */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w2[m];
            REG_D(c) = add16(c, REG_D(c), rd16(c, ea));
            return;
        case 0xC: /* LDD */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w1[m];
            v16 = rd16(c, ea);
            flags_logic16(c, v16);
            REG_D(c) = v16;
            return;
        case 0xD: /* STD */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w1[m];
            v16 = REG_D(c);
            flags_logic16(c, v16);
            wr16(c, ea, v16);
            return;
        case 0xE: /* LDU / LDS */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w1[m];
            v16 = rd16(c, ea);
            flags_logic16(c, v16);
            if (pfx == 0x10) { c->s = v16; c->nmi_armed = 1; } else c->u = v16;
            return;
        case 0xF: /* STU / STS */
            ea = ea_mode(c, m, 2); c->cyc += cyc_w1[m];
            v16 = (pfx == 0x10) ? c->s : c->u;
            flags_logic16(c, v16);
            wr16(c, ea, v16);
            return;
        default: break;
        }
    }

    /* ---- 8 bit accumulator ops ---- */
    {
        uint8_t *acc = grpB ? &REG_B(c) : &REG_A(c);
        ea = ea_mode(c, m, 1);
        c->cyc += cyc_b8[m];
        switch (lo) {
        case 0x0: v8 = RD(ea); *acc = sub8(c, *acc, v8, 0); break;
        case 0x1: v8 = RD(ea); sub8(c, *acc, v8, 0); break;
        case 0x2: v8 = RD(ea); *acc = sub8(c, *acc, v8, (c->cc & CC_C) ? 1 : 0); break;
        case 0x4: *acc &= RD(ea); flags_logic8(c, *acc); break;
        case 0x5: flags_logic8(c, (uint8_t)(*acc & RD(ea))); break;
        case 0x6: *acc = RD(ea); flags_logic8(c, *acc); break;
        case 0x7: flags_logic8(c, *acc); WR(ea, *acc); break;
        case 0x8: *acc ^= RD(ea); flags_logic8(c, *acc); break;
        case 0x9: v8 = RD(ea); *acc = add8(c, *acc, v8, (c->cc & CC_C) ? 1 : 0); break;
        case 0xA: *acc |= RD(ea); flags_logic8(c, *acc); break;
        case 0xB: v8 = RD(ea); *acc = add8(c, *acc, v8, 0); break;
        default: break;
        }
    }
}

/* page 0 instruction (opcode already fetched) */
static void exec_page0(m6809_t *c, uint8_t op)
{
    uint16_t ea;
    uint8_t v8, post;
    int16_t o16;

    if (op >= 0x80) { exec_alu(c, 0, op); return; }

    switch (op >> 4) {
    case 0x0: /* direct RMW */
    case 0x6: /* indexed RMW */
    case 0x7: /* extended RMW */ {
        int kind = op & 0x0f;
        int m = (op >> 4) == 0 ? AM_DIR : (op >> 4) == 6 ? AM_IDX : AM_EXT;
        if (kind == 0xE) {                              /* JMP */
            ea = ea_mode(c, m, 0);
            c->pc = ea;
            c->cyc += (m == AM_EXT) ? 4 : 3;
            return;
        }
        if (!is_unary(kind)) { illegal(c, 0, op); return; }
        ea = ea_mode(c, m, 0);
        c->cyc += (m == AM_EXT) ? 7 : 6;
        if (kind == 0xD) { flags_logic8(c, RD(ea)); return; }          /* TST */
        if (kind == 0xF) { (void)RD(ea); WR(ea, unary(c, kind, 0)); return; } /* CLR */
        v8 = RD(ea);
        WR(ea, unary(c, kind, v8));
        return;
    }
    case 0x4: /* A inherent */
    case 0x5: /* B inherent */ {
        int kind = op & 0x0f;
        uint8_t *acc = (op & 0x10) ? &REG_B(c) : &REG_A(c);
        if (!is_unary(kind)) { illegal(c, 0, op); return; }
        *acc = unary(c, kind, *acc);
        c->cyc += 2;
        return;
    }
    case 0x2: /* short branches */
        o16 = (int8_t)fetch8(c);
        if (branch_cond(c, op & 0x0f)) c->pc = (uint16_t)(c->pc + o16);
        c->cyc += 3;
        return;
    case 0x1:
        switch (op & 0x0f) {
        case 0x2: c->cyc += 2; return;                          /* NOP */
        case 0x3:                                               /* SYNC */
            c->cyc += 4;
            c->state = M6809_SYNC;
            return;
        case 0x6: o16 = (int16_t)fetch16(c);                    /* LBRA */
            c->pc = (uint16_t)(c->pc + o16); c->cyc += 5; return;
        case 0x7: o16 = (int16_t)fetch16(c);                    /* LBSR */
            push16(c, &c->s, c->pc);
            c->pc = (uint16_t)(c->pc + o16); c->cyc += 9; return;
        case 0x9: { /* DAA */
            uint8_t a = REG_A(c), cf = 0;
            uint8_t lsn = a & 0x0f, msn = a & 0xf0;
            unsigned t;
            if ((c->cc & CC_H) || lsn > 9) cf |= 0x06;
            if ((c->cc & CC_C) || msn > 0x90 || (msn > 0x80 && lsn > 9)) cf |= 0x60;
            t = (unsigned)a + cf;
            if (t & 0x100) c->cc |= CC_C;
            REG_A(c) = (uint8_t)t;
            set_nz8(c, REG_A(c));
            c->cyc += 2;
            return;
        }
        case 0xA: c->cc |= fetch8(c); c->cyc += 3; return;      /* ORCC */
        case 0xC: c->cc &= fetch8(c); c->cyc += 3; return;      /* ANDCC */
        case 0xD:                                               /* SEX */
            REG_A(c) = (REG_B(c) & 0x80) ? 0xff : 0x00;
            set_nz16(c, REG_D(c));
            c->cyc += 2;
            return;
        case 0xE: {                                             /* EXG */
            uint16_t r1, r2;
            post = fetch8(c);
            r1 = get_reg(c, post >> 4);
            r2 = get_reg(c, post & 15);
            set_reg(c, post >> 4, r2);
            set_reg(c, post & 15, r1);
            c->cyc += 8;
            return;
        }
        case 0xF:                                               /* TFR */
            post = fetch8(c);
            set_reg(c, post & 15, get_reg(c, post >> 4));
            c->cyc += 6;
            return;
        default: illegal(c, 0, op); return;
        }
    case 0x3:
        switch (op & 0x0f) {
        case 0x0: ea = ea_indexed(c); c->x = ea; c->cyc += 4;
            c->cc = (c->cc & ~CC_Z) | (ea == 0 ? CC_Z : 0); return;
        case 0x1: ea = ea_indexed(c); c->y = ea; c->cyc += 4;
            c->cc = (c->cc & ~CC_Z) | (ea == 0 ? CC_Z : 0); return;
        case 0x2: ea = ea_indexed(c); c->s = ea; c->cyc += 4; c->nmi_armed = 1; return;
        case 0x3: ea = ea_indexed(c); c->u = ea; c->cyc += 4; return;
        case 0x4: v8 = fetch8(c); c->cyc += 5; psh(c, &c->s, c->u, v8); return;
        case 0x5: v8 = fetch8(c); c->cyc += 5; pul(c, &c->s, &c->u, v8); return;
        case 0x6: v8 = fetch8(c); c->cyc += 5; psh(c, &c->u, c->s, v8); return;
        case 0x7: v8 = fetch8(c); c->cyc += 5; pul(c, &c->u, &c->s, v8); return;
        case 0x9: c->pc = pull16(c, &c->s); c->cyc += 5; return;       /* RTS */
        case 0xA: c->x = (uint16_t)(c->x + REG_B(c)); c->cyc += 3; return; /* ABX */
        case 0xB:                                                       /* RTI */
            c->cc = pull8(c, &c->s);
            if (c->cc & CC_E) {
                REG_A(c) = pull8(c, &c->s);
                REG_B(c) = pull8(c, &c->s);
                c->dp = pull8(c, &c->s);
                c->x = pull16(c, &c->s);
                c->y = pull16(c, &c->s);
                c->u = pull16(c, &c->s);
                c->cyc += 15;
            } else {
                c->cyc += 6;
            }
            c->pc = pull16(c, &c->s);
            return;
        case 0xC:                                                       /* CWAI */
            v8 = fetch8(c);
            c->cc &= v8;
            c->cc |= CC_E;
            push_all(c);
            c->cyc += 20;
            c->cwai_ready = 1;
            c->state = M6809_CWAI;
            return;
        case 0xD: {                                                     /* MUL */
            uint16_t r = (uint16_t)(REG_A(c) * REG_B(c));
            REG_D(c) = r;
            c->cc &= ~(CC_Z | CC_C);
            if (r == 0) c->cc |= CC_Z;
            if (r & 0x80) c->cc |= CC_C;
            c->cyc += 11;
            return;
        }
        case 0xF: do_swi(c, VEC_SWI, 1); c->cyc += 19; return;          /* SWI */
        default: illegal(c, 0, op); return;                             /* 38, 3E */
        }
    }
    illegal(c, 0, op);
}

/* page 0x10 / 0x11 */
static void exec_page1(m6809_t *c, uint8_t pfx, uint8_t op)
{
    if (op >= 0x80) {
        int lo = op & 0x0f;
        int grpB = op & 0x40;
        int ok = 0;
        if (pfx == 0x10) {
            if (!grpB) ok = (lo == 0x3 || lo == 0xC || lo == 0xE || lo == 0xF);
            else       ok = (lo == 0xE || lo == 0xF);
        } else {
            if (!grpB) ok = (lo == 0x3 || lo == 0xC);
        }
        if (!ok) { illegal(c, pfx, op); return; }
        exec_alu(c, pfx, op);
        return;
    }
    if (pfx == 0x10 && (op & 0xf0) == 0x20) {                      /* long branches */
        int16_t o = (int16_t)fetch16(c);
        c->cyc += 4;                                  /* + 1 for the prefix = 5 (6 when taken) */
        if (branch_cond(c, op & 0x0f)) { c->pc = (uint16_t)(c->pc + o); c->cyc += 1; }
        return;
    }
    if (op == 0x3f) {                                               /* SWI2 / SWI3 */
        do_swi(c, pfx == 0x10 ? VEC_SWI2 : VEC_SWI3, 0);
        c->cyc += 19;                                 /* + 1 for the prefix = 20 */
        return;
    }
    illegal(c, pfx, op);
}

/* run one instruction */
static void exec_instruction(m6809_t *c)
{
    uint8_t op, pfx = 0;

    c->insn_pc = c->pc;
    op = fetch8(c);
    if (op == 0x10 || op == 0x11) {
        pfx = op;
        op = fetch8(c);
        /* repeated prefixes behave like the last one on real silicon */
        while (op == 0x10 || op == 0x11) { pfx = op; op = fetch8(c); c->cyc += 1; }
        c->cyc += 1;
    }
    c->prefix = pfx;
    c->opcode = op;
    if (pfx) exec_page1(c, pfx, op);
    else     exec_page0(c, op);
    c->insn_count++;
}

/* ------------------------------------------------------------------ */
/* scheduler                                                           */
/* ------------------------------------------------------------------ */
static int pending_int(m6809_t *c)
{
    if (c->nmi_pending && c->nmi_armed) return M6809_NMI_LINE;
    if (c->line[M6809_FIRQ_LINE] && !(c->cc & CC_F)) return M6809_FIRQ_LINE;
    if (c->line[M6809_IRQ_LINE] && !(c->cc & CC_I)) return M6809_IRQ_LINE;
    return -1;
}

int m6809_step(m6809_t *c)
{
    int n, irq;

    c->cyc = 0;
    c->irq_entered = 0;

    if (c->line[M6809_RESET_LINE]) {
        c->state = M6809_RESETTING;
        c->cyc = 1;
        goto done;
    }
    if (c->state == M6809_RESETTING) {
        m6809_reset(c);
        c->cyc = 4;
        goto done;
    }
    if (c->line[M6809_HALT_LINE]) {          /* halt takes effect at a boundary */
        c->cyc = 1;
        goto done;
    }

    c->lic = 1;                              /* we are at an instruction boundary */

    if (c->state == M6809_SYNC) {
        /* any asserted interrupt line wakes the CPU, even when masked */
        if (c->nmi_pending || c->line[M6809_FIRQ_LINE] || c->line[M6809_IRQ_LINE])
            c->state = M6809_RUN;
        else { c->cyc = 1; c->idle_count++; goto done; }
    }

    irq = pending_int(c);
    if (c->state == M6809_CWAI) {
        if (irq < 0) { c->cyc = 1; c->idle_count++; goto done; }
        do_irq_entry(c, irq);
        goto done;
    }
    if (irq >= 0) {
        do_irq_entry(c, irq);
        goto done;
    }

    c->lic = 0;
    exec_instruction(c);
    c->lic = 1;

done:
    n = c->cyc;
    c->total_cycles += (uint64_t)n;
    c->icount -= n;
    return n;
}

int m6809_run(m6809_t *c, int cycles)
{
    int start = c->icount = cycles;
    while (c->icount > 0)
        m6809_step(c);
    return start - c->icount;
}
