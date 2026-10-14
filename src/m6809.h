/* SPDX-License-Identifier: MIT */
/*
 * m6809.h - Motorola MC6809 CPU core (cycle counting, interrupt state machine)
 *
 * Credits / references
 * --------------------
 *  - Instruction semantics (opcode map, flag behaviour, TFR/EXG quirks,
 *    illegal opcode handling, per-instruction cycle counts) follow
 *    "v09s.c" of the sbc09 project:
 *        sbc09 - Lennart Benschop, Jens Diemer, Johann E. Klasek (GPL v2)
 *        https://github.com/cmarrin/6809/tree/main/sbc09
 *    and the OS-9 oriented extensions of Shinji Kono
 *        https://github.com/shinji-kono/sbc09
 *  - The notions of clock / cycle budget / CPU state used here (an
 *    execution budget that is consumed instruction by instruction, a state
 *    machine with RUN / SYNC / CWAI / HALT states, level sensitive IRQ and
 *    FIRQ, edge triggered NMI armed by the first load of S, the "LIC"
 *    last-instruction-cycle output) are modelled after MAME's m6809.cpp:
 *        Nathan Woods, Tim Lindner et al. (BSD-3-Clause)
 *        https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6809/m6809.cpp
 *    No MAME code is copied; this is an independent implementation.
 *
 * This file is distributed under the MIT License (see LICENSES.md).  It is an
 * independent implementation: the sources named above are the specification
 * and the source of ideas, no code of them is copied.
 */
#ifndef M6809_H
#define M6809_H

#include <stdint.h>

/* condition code bits */
#define CC_C 0x01
#define CC_V 0x02
#define CC_Z 0x04
#define CC_N 0x08
#define CC_I 0x10
#define CC_H 0x20
#define CC_F 0x40
#define CC_E 0x80

/* interrupt vectors */
#define VEC_SWI3  0xfff2
#define VEC_SWI2  0xfff4
#define VEC_FIRQ  0xfff6
#define VEC_IRQ   0xfff8
#define VEC_SWI   0xfffa
#define VEC_NMI   0xfffc
#define VEC_RESET 0xfffe

/* input lines (cf. MAME INPUT_LINE_*) */
enum {
    M6809_IRQ_LINE = 0,
    M6809_FIRQ_LINE,
    M6809_NMI_LINE,
    M6809_HALT_LINE,
    M6809_RESET_LINE,
    M6809_NUM_LINES
};

/* CPU execution state (cf. MAME CWAI_IN_PROGRESS / SYNC_IN_PROGRESS) */
enum m6809_state {
    M6809_RUN = 0,      /* fetching and executing instructions            */
    M6809_SYNC,         /* SYNC executed, waiting for an interrupt line   */
    M6809_CWAI,         /* CWAI executed, state already stacked, waiting  */
    M6809_HALTED,       /* /HALT asserted at an instruction boundary      */
    M6809_RESETTING     /* /RESET asserted                                */
};

typedef struct m6809 m6809_t;

typedef uint8_t (*m6809_read_fn)(void *ctx, uint16_t addr);
typedef void    (*m6809_write_fn)(void *ctx, uint16_t addr, uint8_t data);

struct m6809 {
    /* programmer visible registers */
    union {
        uint16_t d;
        struct { uint8_t b, a; } r;          /* little endian host */
    } dd;
    uint16_t x, y, u, s, pc;
    uint8_t  cc, dp;

    /* bus */
    void           *ctx;
    m6809_read_fn   read;
    m6809_write_fn  write;

    /* clock / cycle bookkeeping */
    uint64_t total_cycles;   /* cycles of all completed instructions        */
    int      icount;         /* remaining budget (MAME m_icount)            */
    int      cyc;            /* cycles of the instruction being executed    */
    uint64_t insn_count;
    uint64_t idle_count;     /* steps spent in SYNC / CWAI / HALT              */
    uint8_t  irq_entered;    /* the last step was an interrupt entry (trace aid) */

    /* state */
    enum m6809_state state;
    uint8_t  line[M6809_NUM_LINES];  /* current line levels                 */
    uint8_t  nmi_pending;            /* latched NMI edge                    */
    uint8_t  nmi_armed;              /* NMI is ignored until S is loaded    */
    uint8_t  lic;                    /* last instruction cycle output       */
    uint8_t  cwai_ready;             /* CWAI: state already pushed          */

    uint16_t insn_pc;        /* address of the instruction being executed   */
    uint8_t  opcode;         /* last opcode (after prefix)                  */
    uint8_t  prefix;         /* 0, 0x10 or 0x11                             */
    uint64_t illegal_count;
    uint16_t illegal_pc;

    /* optional callbacks */
    void (*on_illegal)(m6809_t *c, uint16_t pc, uint8_t prefix, uint8_t op);
    void (*on_irq_ack)(m6809_t *c, int line);   /* vector fetch for IRQ/FIRQ/NMI */
};

#define REG_A(c) ((c)->dd.r.a)
#define REG_B(c) ((c)->dd.r.b)
#define REG_D(c) ((c)->dd.d)

void     m6809_init(m6809_t *c, void *ctx, m6809_read_fn rd, m6809_write_fn wr);
void     m6809_reset(m6809_t *c);
void     m6809_set_line(m6809_t *c, int line, int level);

/* Execute one instruction (or interrupt entry, or one idle slot when the
 * CPU is in SYNC/CWAI/HALT).  Returns the number of clock cycles consumed. */
int      m6809_step(m6809_t *c);

/* Run until at least `cycles` cycles have been consumed (MAME execute_run).
 * Returns cycles actually consumed (>= requested, by less than one insn). */
int      m6809_run(m6809_t *c, int cycles);

/* current absolute cycle (valid also while an instruction is executing) */
static inline uint64_t m6809_now(const m6809_t *c) { return c->total_cycles; }

/* true when the CPU sits at an instruction boundary and is idle-waiting */
static inline int m6809_is_waiting(const m6809_t *c)
{
    return c->state == M6809_SYNC || c->state == M6809_CWAI ||
           c->state == M6809_HALTED;
}

/* disassembler (dis6809.c) */
int m6809_disasm(char *buf, int bufsz, uint16_t pc,
                 uint8_t (*rd)(void *, uint16_t), void *ctx);

#endif
