/* SPDX-License-Identifier: MIT */
/*
 * machine.c - bus, MMU, I/O dispatch, timer, RTC, raw disk, scheduler
 *
 * Credits:
 *   Memory map, I/O register protocol, MMU / "CoCo like" task maps, disk
 *   command protocol and VRBF hook: sbc09 src/engine.c, src/io.c, src/v09.c
 *   (L.C. Benschop, Shinji Kono et al., GPL v2).
 *   Clock driven scheduling (a run loop that hands out cycle budgets to the
 *   CPU and advances the peripherals by the cycles actually consumed, idle
 *   skipping while the CPU is in SYNC/CWAI): modelled after MAME's device
 *   scheduler / m6809_base_device::execute_run().
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "machine.h"
#include "console.h"
#include "net.h"
#include "dbg.h"

static uint8_t bitbucket[8192];
static int acia_debug = -1;         /* ACIADEBUG=1: log serial port traffic to stderr */
#define ACIA_DEBUG() (acia_debug < 0 ? (acia_debug = getenv("ACIADEBUG") != NULL) : acia_debug)

/* ------------------------------------------------------------------ */
/* creation                                                            */
/* ------------------------------------------------------------------ */
machine_t *machine_create(int level2, size_t memsize)
{
    machine_t *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    m->level2 = level2;
    m->clock_hz = 1843200;
    m->start_time = (long)time(NULL);
    m->timer_hz = 50;
    m->timer_irq_line = M6809_FIRQ_LINE;
    m->timer_mode = 0;
    m->realtime = 1;
    m->idle_skip = 1;
    if (level2) {
        if (memsize < 512 * 1024) memsize = 512 * 1024;
        if (memsize > 2048 * 1024) memsize = 2048 * 1024;
        m->memsize = memsize;
        m->phymem = calloc(1, 0x80000 + 0x2000 > memsize ? 0x80000 + 0x2000 : memsize);
        if (!m->phymem) { free(m); return NULL; }
        m->mem = m->phymem + 0x38 * 0x2000;       /* 64K window ending at block 3F */
        m->iopage = 0xff80;
        m->romstart = (uint32_t)memsize;          /* everything is RAM */
        m->rommemsize = (uint32_t)memsize;
    } else {
        m->mem = m->mem1;
        m->iopage = 0xe000;
        m->romstart = 0x10000;
    }
    m6809_init(&m->cpu, m, (m6809_read_fn)machine_read, (m6809_write_fn)machine_write);
    m->cpu.on_irq_ack = machine_irq_ack;
    return m;
}

static uint8_t *iomem(machine_t *m) { return m->level2 ? m->mem + 0xfe00 : m->mem1 + 0xe000; }

int machine_load_rom(machine_t *m, const char *file, long romstart)
{
    FILE *f = fopen(file, "rb");
    long len;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (m->level2) {
        /* discless boot image: placed at physical $7E000, boot reads the
         * rest via the "extended ROM" blocks starting at $80000.       */
        size_t off = 0x38 * 0x2000 + 0xe000;
        if (off + len > m->memsize) {
            fprintf(stderr, "ROM image too large for %zu bytes of memory\n", m->memsize);
            fclose(f);
            return -1;
        }
        if (fread(m->mem + 0xe000, 1, len, f) != (size_t)len) { fclose(f); return -1; }
        if (romstart != 0x8000) m->romstart = (uint32_t)romstart;
        m->mem[0xff90] = 0;
        m->mem[0xffa7] = 0x3f;
    } else {
        if (romstart == 0x8000) romstart = 0x10000 - len;
        m->romstart = (uint32_t)romstart;
        if (fread(m->mem + (romstart & 0xffff), 1, len, f) != (size_t)len) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

void machine_set_timer(machine_t *m, long hz, int irq_line, int mode)
{
    m->timer_hz = hz > 0 ? hz : 50;
    m->timer_irq_line = irq_line;
    m->timer_mode = mode;
    m->timer_period = (uint64_t)m->clock_hz / (uint64_t)m->timer_hz;
}

void machine_reset(machine_t *m)
{
    uint8_t *io = iomem(m);
    (void)io;
    m->timer_period = (uint64_t)m->clock_hz / (uint64_t)m->timer_hz;
    if (m->level2) m->mmu = m->mem + 0xffa0;
    m6809_reset(&m->cpu);
    if (!(m->timer_mode & 3)) {     /* default (monitor ROM): free running timer */
        m->timer_running = 1;
        m->timer_next = m->cpu.total_cycles + m->timer_period;
    }
}

/* ------------------------------------------------------------------ */
/* address translation                                                 */
/* ------------------------------------------------------------------ */
static inline uint32_t phys_l2(machine_t *m, uint16_t a, const uint8_t *map)
{
    if ((a & 0xfe00) == 0xfe00) return 0x38u * 0x2000u + a;
    return ((uint32_t)map[a >> 13] << 13) + (a & 0x1fff);
}

uint8_t *machine_ptr(machine_t *m, uint16_t addr, const uint8_t *map)
{
    if (!m->level2) return &m->mem1[addr];
    uint32_t p = phys_l2(m, addr, map ? map : m->mmu);
    if (p >= m->memsize) return &bitbucket[addr & 0x1fff];
    return m->phymem + p;
}

uint8_t machine_peek_phys(machine_t *m, uint32_t p)
{
    return (m->level2 && p < m->memsize) ? m->phymem[p] : 0xff;
}

uint8_t machine_peek(machine_t *m, uint16_t a)
{
    if (!m->level2) return m->mem1[a];
    uint32_t p = phys_l2(m, a, m->mmu);
    return p < m->memsize ? m->phymem[p] : 0xff;
}

/* ------------------------------------------------------------------ */
/* I/O                                                                 */
/* ------------------------------------------------------------------ */
static int acia_index(int off)
{
    if (off >= 0 && off < 2) return 0;
    if (off >= 4 && off < 10) return 1 + (off - 4) / 2;
    return -1;
}

static uint8_t io_read(machine_t *m, uint16_t addr)
{
    uint8_t *io = iomem(m);
    int off = (int)addr - (int)m->iopage;
    m->io_reads++;
    if (off >= 0 && off < 0x80) {
        int ai = acia_index(off);
        if (ai >= 0 && (ai == 0 || m->acia[ai].kind)) return acia_read(m, ai, off & 1);
        /* an unconfigured network port is not there: plain memory like on the reference machine */
    }
    return io[addr & 0x1ff];
}

void do_timer_write(machine_t *m, int off, uint8_t v)
{
    uint8_t *io = iomem(m);
    uint8_t *r = io + (m->iopage & 0x1ff) + 0x30;
    if (off == 0) {
        if (v == 0x8f) {
            if (!(m->timer_mode & 2)) return;
            m->timer_running = 1;
            m->timer_next = m->cpu.total_cycles + m->timer_period;
            m->timer_pending = 0;
            m6809_set_line(&m->cpu, m->timer_irq_line, 0);
            r[0] = v;
        } else if (v == 0x80) {
            m->timer_running = 0;
            m->timer_pending = 0;
            m6809_set_line(&m->cpu, m->timer_irq_line, 0);
            r[0] = v;
        } else if (v == 0x04) {
            /* the RTC runs with the emulated time (start time of the host + cycles / clock), so that the OS-9 time agrees with
             * its ticks and sleeps also at full speed (-max); a fixed time for cross check runs */
            time_t t = m->det_icount ? (time_t)1790000000 : (time_t)(m->start_time + (long)(m->cpu.total_cycles / (uint64_t)m->clock_hz));
            struct tm tmv;
            localtime_r(&t, &tmv);
            r[1] = (uint8_t)tmv.tm_year;
            r[2] = (uint8_t)(tmv.tm_mon + 1);
            r[3] = (uint8_t)tmv.tm_mday;
            r[4] = (uint8_t)tmv.tm_hour;
            r[5] = (uint8_t)tmv.tm_min;
            r[6] = (uint8_t)tmv.tm_sec;
        } else {
            r[0] = v;
        }
    } else {
        r[off] = v;
    }
}

static void io_write(machine_t *m, uint16_t addr, uint8_t v)
{
    uint8_t *io = iomem(m);
    int off = (int)addr - (int)m->iopage;
    m->io_writes++;
    if (off >= 0 && off < 0x80) {
        int ai = acia_index(off);
        if (ai >= 0 && (ai == 0 || m->acia[ai].kind)) {
            if (off & 1) acia_write(m, ai, 1, v);
            return;
        }
        if (off >= 0x40 && off < 0x50) {
            if (off == 0x40) do_disk_write(m, 0, v);
            else io[addr & 0x1ff] = v;
            return;
        }
        if (off == 0x0f) {                       /* power control: $A5 = power off */
            if (v == 0xa5) { m->stop = 1; m->quit = 1; m->exit_code = 0; }
            return;
        }
        if (off >= 0x30 && off < 0x40) { do_timer_write(m, off - 0x30, v); return; }
        if (m->level2 && off >= 0x10 && off < 0x30) {
            if (off == 0x11) m->mmu = (v & 1) ? io + 0x1a8 : io + 0x1a0;
            io[addr & 0x1ff] = v;
            return;
        }
        if (!m->level2 && off >= 0x10 && off < 0x30) return;   /* no MMU on L1 */
        if (!m->level2) return;                                 /* L1: ignore */
    }
    io[addr & 0x1ff] = v;
}

uint8_t machine_read(machine_t *m, uint16_t addr)
{
    if (!m->level2) {
        if ((addr & 0xfe00) == (m->iopage & 0xfe00)) return io_read(m, addr);
        return m->mem1[addr];
    }
    if ((addr & 0xfe00) == 0xfe00) return io_read(m, addr);
    {
        uint32_t p = ((uint32_t)m->mmu[addr >> 13] << 13) + (addr & 0x1fff);
        return p < m->rommemsize ? m->phymem[p] : 0xff;
    }
}

void machine_write(machine_t *m, uint16_t addr, uint8_t v)
{
    if (m->watch_hi && addr >= m->watch_lo && addr <= m->watch_hi)
        fprintf(stderr, "WATCH write [%04X]=%02X by pc=%04X (opcode %02X) S=%04X\n", addr, v,
                m->cpu.insn_pc, m->cpu.opcode, m->cpu.s);
    if (!m->level2) {
        if ((addr & 0xfe00) == (m->iopage & 0xfe00)) { io_write(m, addr, v); return; }
        if (addr < m->romstart) m->mem1[addr] = v;
        return;
    }
    if ((addr & 0xfe00) == 0xfe00) { io_write(m, addr, v); return; }
    {
        uint32_t p = ((uint32_t)m->mmu[addr >> 13] << 13) + (addr & 0x1fff);
        if (p < m->romstart && p < m->memsize) m->phymem[p] = v;
    }
}

/* ------------------------------------------------------------------ */
/* raw disk + VRBF command port                                        */
/* ------------------------------------------------------------------ */
void do_disk_write(machine_t *m, int off, uint8_t cmd)
{
    uint8_t *io = iomem(m);
    uint8_t *r = io + (m->iopage & 0x1ff) + 0x40;
    int drv, ok = 0;
    long lsn;
    uint16_t buf;
    uint8_t sect[SECSIZE];
    (void)off;

    if (cmd != 0x81 && cmd != 0x55) {      /* VRBF command (0xd1..0xdd) */
        r[0] = cmd;
        do_vdisk(m, cmd);
        return;
    }
    drv = r[1];
    lsn = ((long)r[2] << 16) | (r[3] << 8) | r[4];
    buf = (uint16_t)((r[5] << 8) | r[6]);
    if (getenv("DISKDEBUG")) fprintf(stderr, "disk %s drv=%d lsn=%ld buf=%04x\n", cmd == 0x81 ? "read " : "write", drv, lsn, buf);
    if (drv > 1 || !m->disk[drv]) goto error;
    if (fseek(m->disk[drv], lsn * SECSIZE, SEEK_SET) != 0) goto error;
    if (cmd == 0x81) {
        size_t n = fread(sect, 1, SECSIZE, m->disk[drv]);
        if (n < SECSIZE) memset(sect + n, 0, SECSIZE - n);
        for (int i = 0; i < SECSIZE; i++)
            *machine_ptr(m, (uint16_t)(buf + i), NULL) = sect[i];
        ok = 1;
    } else {
        for (int i = 0; i < SECSIZE; i++)
            sect[i] = *machine_ptr(m, (uint16_t)(buf + i), NULL);
        if (fwrite(sect, 1, SECSIZE, m->disk[drv]) != SECSIZE) goto error;
        fflush(m->disk[drv]);
        ok = 1;
    }
error:
    r[0] = ok ? 0 : 0xff;
}

/* ------------------------------------------------------------------ */
/* devices advance                                                     */
/* ------------------------------------------------------------------ */
void machine_irq_ack(m6809_t *c, int line)
{
    machine_t *m = c->ctx;
    if (line == m->timer_irq_line && m->timer_pending) {
        m->timer_pending = 0;
        m6809_set_line(c, line, 0);
    }
}

static void timer_fire(machine_t *m)
{
    uint8_t *io = iomem(m);
    if (ACIA_DEBUG()) fprintf(stderr, "TIMER fire at cycle %llu pc=%04x\n", (unsigned long long)m->cpu.total_cycles, m->cpu.pc);
    io[(m->iopage & 0x1ff) + 0x30] |= 0x10;
    m->timer_pending = 1;
    m6809_set_line(&m->cpu, m->timer_irq_line, 1);
}

static int dev_timer(machine_t *m)
{
    int changed = 0;
    uint64_t now = m->cpu.total_cycles;

    if (m->det_icount) {
        /* -icount: the timer ticks every N executed instructions (idle steps count
         * too), an IRQ that finds I set is lost - this mirrors the instruction
         * counted timer of the cross check build of the reference simulator */
        uint64_t k = m->cpu.insn_count + m->cpu.idle_count + 1;
        if (m->dettype_text && m->dettype_text[m->dettype_pos] &&
            k >= m->dettype_start + m->dettype_pos * m->dettype_gap) {
            int ch = (uint8_t)m->dettype_text[m->dettype_pos++];
            if (ch == '\\' && m->dettype_text[m->dettype_pos] == 'r') { ch = '\r'; m->dettype_pos++; }
            console_rx_push(&m->acia[0], (uint8_t)ch);
        }
        if (m->timer_running && k % (uint64_t)m->det_icount == 0 && k != m->det_next) {
            m->det_next = k;
            m->timer_fire_count++;
            uint8_t *io = iomem(m);
            io[(m->iopage & 0x1ff) + 0x30] |= 0x10;
            if (!(m->cpu.cc & CC_I) || m->timer_irq_line == M6809_FIRQ_LINE) {
                m->timer_pending = 1;
                m6809_set_line(&m->cpu, m->timer_irq_line, 1);
            }
            changed = 1;
        }
    } else if (m->timer_running && now >= m->timer_next) {
        m->timer_next += m->timer_period;
        if (m->timer_next <= now) m->timer_next = now + m->timer_period;  /* we were stalled */
        timer_fire(m);
        changed = 1;
    }
    return changed;
}

static void dev_io(machine_t *m)
{
    if (console_script_active()) console_script_step(m);
    for (int i = 0; i < MAX_ACIA; i++)
        if (m->acia[i].kind) console_poll(m, i);
    net_poll(m);
}

int dev_tick(machine_t *m)
{
    int changed = dev_timer(m);
    dev_io(m);
    return changed;
}

/* ------------------------------------------------------------------ */
/* ACIA register access                                                */
/* ------------------------------------------------------------------ */
static int rx_avail(acia_t *a) { return a->rxhead != a->rxtail; }

uint8_t acia_read(machine_t *m, int idx, int reg)
{
    acia_t *a = &m->acia[idx];
    if (!a->kind) return reg ? 0 : 0x02;
    if (!rx_avail(a)) console_poll(m, idx);
    if (reg == 0) {
        /* status: RDRF only once the baud rate pacing allows the next character */
        int ready = rx_avail(a) && m->cpu.total_cycles >= a->next_rx_cycle;
        uint8_t st = 0x02;                       /* TDRE always set     */
        if (ready) st |= 0x01;                   /* RDRF                */
        if (a->kind == ACIA_NET) st |= net_status_bits(m, idx);
        if (a->kind == ACIA_STDIO) a->last_rx = st & 3;   /* like the reference: an empty data register returns the last status */
        return st;
    }
    /* data: a waiting character is always delivered - what the status register
     * announced must still be there when the guest reads the data register */
    if (rx_avail(a)) {
        a->last_rx = a->rxq[a->rxtail];
        if (ACIA_DEBUG()) fprintf(stderr, "acia%d rx %02x at cycle %llu pc=%04x\n", idx, a->last_rx,
                                         (unsigned long long)m->cpu.total_cycles, m->cpu.insn_pc);
        a->rxtail = (a->rxtail + 1) % sizeof a->rxq;
        a->next_rx_cycle = m->cpu.total_cycles + a->baud_cycles;
    } else if (a->kind == ACIA_NET) {
        net_hup_ack(m, idx);                  /* reading an empty data register acks HUP */
    }
    return (uint8_t)a->last_rx;
}

void acia_write(machine_t *m, int idx, int reg, uint8_t v)
{
    acia_t *a = &m->acia[idx];
    (void)reg;
    if (!a->kind) return;
    if (ACIA_DEBUG()) {
        uint16_t sp = m->cpu.s;
        fprintf(stderr, "acia%d tx %02x at cycle %llu pc=%04x s=%04x stack:", idx, v,
                (unsigned long long)m->cpu.total_cycles, m->cpu.insn_pc, sp);
        for (int i = 0; i < 12; i += 2)
            fprintf(stderr, " %02x%02x", machine_peek(m, (uint16_t)(sp + i)), machine_peek(m, (uint16_t)(sp + i + 1)));
        fprintf(stderr, " D.Proc=%02x%02x\n", machine_peek(m, 0x4b), machine_peek(m, 0x4c));
    }
    console_tx(m, idx, v);
}

/* ------------------------------------------------------------------ */
/* run loop                                                            */
/* ------------------------------------------------------------------ */
static uint64_t host_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int needs_hooks(machine_t *m)
{
    return m->trace || m->step_mode || m->nbrk || m->det_icount || dbg_os9trace;
}

/* number of cycles until the next device event (or 'slice' if none) */
static uint64_t cycles_to_event(machine_t *m, uint64_t slice)
{
    uint64_t now = m->cpu.total_cycles;
    uint64_t lim = slice;
    if (m->timer_running && !m->det_icount) {
        uint64_t d = m->timer_next > now ? m->timer_next - now : 1;
        if (d < lim) lim = d;
    }
    return lim ? lim : 1;
}

int machine_run(machine_t *m, uint64_t max_cycles)
{
    static uint64_t host_t0;
    static uint64_t cyc_t0;
    uint64_t start = m->cpu.total_cycles;
    uint64_t stop_at = max_cycles ? start + max_cycles : 0;

    if (!host_t0) { host_t0 = host_ns(); cyc_t0 = m->cpu.total_cycles; }
    m->stop = 0;

    while (!m->stop) {
        uint64_t budget = cycles_to_event(m, 1000);
        if (stop_at && m->cpu.total_cycles + budget > stop_at)
            budget = stop_at > m->cpu.total_cycles ? stop_at - m->cpu.total_cycles : 1;

        if (m6809_is_waiting(&m->cpu) && !m->cpu.line[M6809_RESET_LINE]) {
            /* idle: nothing can happen before the next timer tick or a
             * host event; skip time instead of spinning */
            int irq = m->cpu.nmi_pending || m->cpu.line[M6809_IRQ_LINE] || m->cpu.line[M6809_FIRQ_LINE];
            if (!irq && m->idle_skip && !m->det_icount) {
                uint64_t skip = cycles_to_event(m, 20000);
                if (m->net_active && !m->realtime) {   /* peers answer in host time: do not run ahead */
                    uint64_t ms = (uint64_t)m->clock_hz / 1000;
                    if (skip > ms) skip = ms;
                    console_wait(m, 1000);
                }
                m->cpu.total_cycles += skip;
                dev_tick(m);
                if (m->realtime) {
                    uint64_t target = host_t0 + (m->cpu.total_cycles - cyc_t0) * 1000000000ull / (uint64_t)m->clock_hz;
                    uint64_t now = host_ns();
                    if (target > now) console_wait(m, (long)((target - now) / 1000));
                }
                if (console_escape) { dbg_escape(m); }
                if (stop_at && m->cpu.total_cycles >= stop_at) break;
                continue;
            }
        }

        if (needs_hooks(m)) {
            uint64_t n = 0;
            while (n < budget && !m->stop) {
                if (dbg_before_insn(m)) { m->stop = 1; break; }
                if (m->max_count && m->cpu.insn_count + m->cpu.idle_count >= m->max_count) { m->stop = 1; if (getenv("MAXMON")) m->want_monitor = 1; else m->quit = 1; break; }
                n += (uint64_t)m6809_step(&m->cpu);
                if (m->det_icount) dev_timer(m);
            }
        } else {
            m6809_run(&m->cpu, (int)budget);
        }
        dev_tick(m);

        if (m->realtime && m->cpu.state == M6809_RUN) {
            uint64_t target = host_t0 + (m->cpu.total_cycles - cyc_t0) * 1000000000ull / (uint64_t)m->clock_hz;
            uint64_t now = host_ns();
            if (target > now + 200000) console_wait(m, (long)((target - now) / 1000));
            else if (now > target + 500000000ull) { host_t0 = now; cyc_t0 = m->cpu.total_cycles; }
        }
        if (console_escape) dbg_escape(m);
        if (console_script_active() && console_script_status() != 0) break;
        if (stop_at && m->cpu.total_cycles >= stop_at) break;
    }
    return m->exit_code;
}
