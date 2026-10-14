/* SPDX-License-Identifier: MIT */
/*
 * machine.h - the simulated single board computer
 *
 * Two machine models are provided, both compatible (register for register)
 * with the "v09" / "v09c" simulators of Shinji Kono's sbc09 fork
 * (https://github.com/shinji-kono/sbc09), so that the OS-9 Level 1 / Level 2
 * ROM images and the virtual RBF file manager (VRBF) of that project run
 * unmodified:
 *
 *   Level 1 model : 64 KB flat memory, ROM at the top (write protected),
 *                   I/O page at $E000-$E1FF.
 *   Level 2 model : CoCo3 style 2 MB physical memory, two task maps of eight
 *                   8 KB MMU blocks, fixed RAM/IO page at $FE00-$FFFF,
 *                   I/O registers based at $FF80.
 *
 * I/O map (offsets from the I/O base, i.e. $E000 or $FF80):
 *    00 ACIA0 control/status      01 ACIA0 data      (console)
 *    04 ACIA1 ctl/status          05 ACIA1 data      (network port 1)
 *    06/07 ACIA2                  08/09 ACIA3        (network ports 2,3)
 *    11 MMU task register         20-27 system map   28-2F user map   (L2)
 *    30 timer control/status      31-36 YY MM DD hh mm ss (host RTC)
 *    40 disk / VRBF command       41-47 disk / VRBF parameters
 *
 * Credits: register layout and behaviour from sbc09 src/io.c, src/engine.c,
 * src/v09.c (L.C. Benschop, Shinji Kono et al., GPL v2); the clock / cycle
 * driven scheduling is modelled after MAME's device scheduler.
 */
#ifndef MACHINE_H
#define MACHINE_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "m6809.h"

#define MAX_ACIA 4
#define SECSIZE  256

struct machine;
typedef struct acia {
    /* RX side: bytes waiting for the guest */
    uint8_t  rxq[4096];
    unsigned rxhead, rxtail;
    int      kind;            /* see console.h: ACIA_* */
    void    *priv;            /* backend private data  */
    uint64_t next_rx_cycle;   /* pacing: earliest cycle of next RX byte */
    uint64_t baud_cycles;     /* cycles per character (0 = unlimited)   */
    int      last_rx;         /* last byte presented on data register    */
} acia_t;

typedef struct machine {
    m6809_t  cpu;

    int      level2;               /* 0 = Level 1 model, 1 = Level 2 (MMU) */
    uint16_t iopage;               /* I/O base: $E000 or $FF80             */

    /* memory */
    uint8_t *phymem;               /* L2: physical memory, L1: NULL        */
    size_t   memsize;              /* L2: bytes of physical memory         */
    uint8_t  mem1[65536];          /* L1 flat memory                       */
    uint8_t *mem;                  /* L1: mem1, L2: phymem + 0x70000       */
    uint32_t romstart;             /* L1: first protected address; L2: >=  */
    uint32_t rommemsize;
    uint8_t *mmu;                  /* L2: currently active MMU map (8 bytes) */

    /* peripherals */
    acia_t   acia[MAX_ACIA];
    int      timer_mode;           /* bit1: start on command $8F, bit0: off */
    int      timer_irq_line;       /* M6809_IRQ_LINE or M6809_FIRQ_LINE      */
    int      timer_running;
    uint64_t timer_period;         /* cycles per tick                       */
    uint64_t timer_next;           /* absolute cycle of the next tick       */
    int      timer_pending;        /* interrupt request latched             */
    long     timer_hz;

    FILE    *disk[2];              /* raw sector images (-0 / -1)           */

    /* clock */
    long     clock_hz;             /* CPU clock (default 1.8432 MHz)        */

    /* pacing and options */
    int      realtime;
    int      idle_skip;
    int      det_icount;           /* debug: timer every N instructions      */
    long     start_time;           /* host time at start: the RTC is this + emulated time */
    uint64_t det_next;

    /* statistics / debug */
    uint64_t io_reads, io_writes;
    int      stop;                 /* request to leave the run loop          */
    int      exit_code;
    int      debug_vdisk;
    int      trace;                /* instruction trace on                   */
    FILE    *tracefile;
    uint16_t trace_lo, trace_hi;
    int      step_mode;
    int      step_count;
    int      want_monitor;
    int      quit;
    uint64_t timer_fire_count;
    unsigned long dettype_start, dettype_gap;   /* -dettype: instruction counted type-in */
    const char  *dettype_text;
    size_t       dettype_pos;
    uint64_t max_count;           /* debug: stop after this many instructions + idle steps */
    int      net_active;           /* network ports configured: idle time follows the host clock */
    int      watch_lo, watch_hi;   /* -watch: log CPU writes to [lo,hi] (-1 = off) */
    uint16_t brk[16];
    int      nbrk;
    int      watch_hit;
    int      escape_pending;

    /* hooks into the other modules */
    void    *vdisk;
    void    *net;
} machine_t;

/* creation / configuration */
machine_t *machine_create(int level2, size_t memsize);
int  machine_load_rom(machine_t *m, const char *file, long romstart);
void machine_reset(machine_t *m);
void machine_set_timer(machine_t *m, long hz, int irq_line, int mode);

/* bus accessors (what the CPU sees) */
uint8_t machine_read(machine_t *m, uint16_t addr);
void    machine_write(machine_t *m, uint16_t addr, uint8_t v);
/* host pointer for a CPU address using a given map (L2) / flat (L1). */
uint8_t *machine_ptr(machine_t *m, uint16_t addr, const uint8_t *map);
/* side-effect free read for debuggers / disassembler */
uint8_t machine_peek(machine_t *m, uint16_t addr);
uint8_t machine_peek_phys(machine_t *m, uint32_t paddr);

/* scheduler */
int  machine_run(machine_t *m, uint64_t max_cycles);  /* returns when stop set / cycles done */
void machine_irq_ack(m6809_t *c, int line);

/* device entry points */
void do_timer_write(machine_t *m, int off, uint8_t v);
void do_disk_write(machine_t *m, int off, uint8_t v);
int  dev_tick(machine_t *m);   /* advance devices to current CPU time; non-zero = state changed */

/* console / acia: see console.h */
uint8_t acia_read(machine_t *m, int idx, int reg);
void    acia_write(machine_t *m, int idx, int reg, uint8_t v);

/* vdisk.c */
int  vdisk_set_root(int drv, const char *dir);
void do_vdisk(machine_t *m, uint8_t cmd);

#endif
