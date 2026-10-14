/* SPDX-License-Identifier: MIT */
/*
 * dbg.c - instruction trace, breakpoints and the interactive monitor
 *
 * Credits: command set and trace line format are modelled on the "v09>"
 * monitor of sbc09 src/trace.c (Shinji Kono, GPL v2): s n f b B d c x xi xp
 * 0 1 L S U D R q h.  Implemented independently.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "dbg.h"
#include "console.h"

static FILE *logfile;
static uint8_t rdfn_peek(void *ctx, uint16_t a) { return machine_peek((machine_t *)ctx, a); }

static int step_over_pc = -1;       /* 'n': run until pc == this */
static int skip_brk_pc = -1;        /* do not break again at this pc on resume */

static void print_regs(machine_t *m, FILE *fp)
{
    m6809_t *c = &m->cpu;
    fprintf(fp, "x=%04x y=%04x u=%04x s=%04x a=%02x b=%02x cc=%02x dp=%02x pc=",
            c->x, c->y, c->u, c->s, REG_A(c), REG_B(c), c->cc, c->dp);
}

void dbg_trace(machine_t *m, FILE *fp)
{
    char text[96];
    int n, i;
    uint16_t pc = m->cpu.pc;
    n = m6809_disasm(text, sizeof text, pc, rdfn_peek, m);
    print_regs(m, fp);
    fprintf(fp, "%04X: ", pc);
    for (i = 0; i < 4; i++) {
        if (i < n) fprintf(fp, "%02X ", machine_peek(m, (uint16_t)(pc + i)));
        else fprintf(fp, "   ");
    }
    fprintf(fp, " %s\n", text);
}

int dbg_set_break(machine_t *m, uint16_t addr)
{
    if (m->nbrk >= 16) return -1;
    m->brk[m->nbrk++] = addr;
    return 0;
}

/* -os9trace: log every OS-9 system call (SWI2 + service code) of user state code */
int dbg_os9trace;

static int os9_ret_pc = -1;
static const char *os9_ret_name;

static void os9_log(machine_t *m)
{
    m6809_t *c = &m->cpu;
    uint16_t pc = c->pc;
    int code = machine_peek(m, (uint16_t)(pc + 2));
    const char *nm = os9_svc_name(code);
    char str[24];
    int i;
    /* the string argument (path name / module name) lives in X for most calls */
    for (i = 0; i < 20; i++) {
        uint8_t ch = machine_peek(m, (uint16_t)(c->x + i));
        if (ch < 0x20 || ch > 0x7e) { if (ch & 0x80) str[i++] = (char)(ch & 0x7f); break; }
        str[i] = (char)ch;
    }
    str[i] = 0;
    fprintf(stderr, "os9 %04X %-9s A=%02X B=%02X X=%04X Y=%04X U=%04X \"%s\"\n", pc,
            nm ? nm : "?", REG_A(c), REG_B(c), c->x, c->y, c->u, str);
    os9_ret_pc = (uint16_t)(pc + 3);
    os9_ret_name = nm ? nm : "?";
}

int dbg_before_insn(machine_t *m)
{
    m6809_t *c = &m->cpu;
    uint16_t pc = c->pc;
    int i;

    if (dbg_os9trace && os9_ret_pc == pc && !m6809_is_waiting(c)) {
        fprintf(stderr, "     <- %s%s CC=%02X A=%02X B=%02X X=%04X Y=%04X U=%04X\n", os9_ret_name,
                (c->cc & CC_C) ? " ERROR" : "", c->cc, REG_A(c), REG_B(c), c->x, c->y, c->u);
        os9_ret_pc = -1;
    }
    if (dbg_os9trace && !m6809_is_waiting(c) && machine_peek(m, pc) == 0x10 &&
        machine_peek(m, (uint16_t)(pc + 1)) == 0x3f)
        os9_log(m);

    if (m->trace && m->tracefile && pc >= m->trace_lo && pc <= m->trace_hi &&
        !m6809_is_waiting(c) && !c->irq_entered)
        dbg_trace(m, m->tracefile);
    if (m6809_is_waiting(c)) return 0;
    if (step_over_pc >= 0) {
        if (pc == (uint16_t)step_over_pc) {
            step_over_pc = -1; m->want_monitor = 1; return 1;
        }
    }
    if (m->step_mode) {
        if (--m->step_count <= 0) {
            m->step_mode = 0; m->want_monitor = 1; return 1;
        }
    }
    for (i = 0; i < m->nbrk; i++) {
        if (m->brk[i] == pc) {
            if (skip_brk_pc == pc) { skip_brk_pc = -1; continue; }
            printf("\nbreak at %04X\n", pc);
            m->want_monitor = 1;
            return 1;
        }
    }
    skip_brk_pc = -1;
    return 0;
}

void dbg_escape(machine_t *m)
{
    console_escape = 0;
    m->want_monitor = 1;
    m->stop = 1;
}

/* ------------------------------------------------------------------ */
static void dump_mem(machine_t *m, uint16_t a, int n, int phys, uint32_t pa)
{
    int i;
    while (n > 0) {
        printf(phys ? "%06X: " : "%04X: ", phys ? pa : a);
        for (i = 0; i < 16; i++)
            if (i < n) printf("%02X ", phys ? machine_peek_phys(m, pa + i) : machine_peek(m, (uint16_t)(a + i)));
            else printf("   ");
        printf("|");
        for (i = 0; i < 16 && i < n; i++) {
            uint8_t ch = phys ? machine_peek_phys(m, pa + i) : machine_peek(m, (uint16_t)(a + i));
            putchar(ch >= 32 && ch < 127 ? ch : '.');
        }
        printf("|\n");
        a += 16; pa += 16; n -= 16;
    }
}

static void disasm_range(machine_t *m, uint16_t a, int count)
{
    char text[96];
    while (count-- > 0) {
        int n = m6809_disasm(text, sizeof text, a, rdfn_peek, m), i;
        printf("%04X: ", a);
        for (i = 0; i < 4; i++) {
            if (i < n) printf("%02X ", machine_peek(m, (uint16_t)(a + i)));
            else printf("   ");
        }
        printf(" %s\n", text);
        a += (uint16_t)n;
    }
}

static int load_srec(machine_t *m, const char *file)
{
    FILE *f = fopen(file, "r");
    char line[600];
    int cnt = 0;
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        unsigned len, addr, i, b;
        char *p = line;
        if (p[0] != 'S' || (p[1] != '1' && p[1] != '2')) continue;
        if (p[1] == '1') {
            if (sscanf(p + 2, "%2x%4x", &len, &addr) != 2) continue;
            p += 8; len -= 3;
        } else {
            unsigned a3;
            if (sscanf(p + 2, "%2x%6x", &len, &a3) != 2) continue;
            addr = a3 & 0xffff; p += 10; len -= 4;
        }
        for (i = 0; i < len; i++, p += 2) {
            if (sscanf(p, "%2x", &b) != 1) break;
            machine_write(m, (uint16_t)(addr + i), (uint8_t)b);
            cnt++;
        }
    }
    fclose(f);
    return cnt;
}

static int save_srec(machine_t *m, const char *file, uint16_t a, uint32_t n)
{
    FILE *f = fopen(file, "w");
    uint32_t i;
    if (!f) return -1;
    while (n) {
        unsigned len = n > 16 ? 16 : (unsigned)n;
        unsigned sum = len + 3 + (a >> 8) + (a & 255);
        fprintf(f, "S1%02X%04X", len + 3, a);
        for (i = 0; i < len; i++) {
            uint8_t b = machine_peek(m, (uint16_t)(a + i));
            fprintf(f, "%02X", b);
            sum += b;
        }
        fprintf(f, "%02X\n", (~sum) & 0xff);
        a += (uint16_t)len; n -= len;
    }
    fprintf(f, "S9030000FC\n");
    fclose(f);
    return 0;
}

static void help(void)
{
    puts("  s [n]        step n instructions (default 1)\n"
         "  n            step over a call / OS-9 call\n"
         "  c            continue\n"
         "  r            show registers\n"
         "  b [adr]      set breakpoint (no argument: list)\n"
         "  B            list breakpoints         d n   delete breakpoint n\n"
         "  x [adr [n]]  dump n bytes             xi [adr [n]]  disassemble\n"
         "  xp blk [off [n]]  dump physical memory (Level 2)    mmu  show MMU maps\n"
         "  w adr val    write byte               t [on|off] [file]  trace\n"
         "  0 file / 1 file   disk images         L file  log console input\n"
         "  S file       type-in file             U file  load S-record\n"
         "  D file adr n save S-record            R   reset\n"
         "  q | X        quit                     h | ?   help");
}

static long num(const char *s) { return s ? strtol(s, NULL, 0) : 0; }

void dbg_monitor(machine_t *m)
{
    char line[256];
    m6809_t *c = &m->cpu;
    uint16_t last_addr = c->pc;

    console_cooked_begin();
    m->want_monitor = 0;
    m->step_mode = 0;
    step_over_pc = -1;
    printf("\n");
    {
        char text[96];
        print_regs(m, stdout);
        m6809_disasm(text, sizeof text, c->pc, rdfn_peek, m);
        printf("%04X: %s\n", c->pc, text);
    }
    for (;;) {
        char *argv[8];
        int argc = 0;
        char *t;
        printf("v09>");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) { m->exit_code = 0; m->stop = 1; m->quit = 1; m->want_monitor = 0; goto out_quit; }
        for (t = strtok(line, " \t\r\n"); t && argc < 8; t = strtok(NULL, " \t\r\n")) argv[argc++] = t;
        if (!argc) { argv[0] = "s"; argc = 1; }
        switch (argv[0][0]) {
        case 'h': case '?': help(); break;
        case 'q': case 'X':
            m->stop = 1; m->quit = 1; m->exit_code = 0; goto out_quit;
        case 'r': {
            char text[96];
            print_regs(m, stdout);
            m6809_disasm(text, sizeof text, c->pc, rdfn_peek, m);
            printf("%04X: %s\n", c->pc, text);
            break;
        }
        case 's': {
            int n = argc > 1 ? (int)num(argv[1]) : 1;
            m->step_mode = 1; m->step_count = n > 0 ? n : 1;
            m->trace = 1; if (!m->tracefile) m->tracefile = stdout;
            goto resume;
        }
        case 'n': {
            char text[96];
            int len = m6809_disasm(text, sizeof text, c->pc, rdfn_peek, m);
            if (!strncmp(text, "JSR", 3) || !strncmp(text, "BSR", 3) || !strncmp(text, "LBSR", 4) || !strncmp(text, "OS9", 3)) {
                step_over_pc = (uint16_t)(c->pc + len);
                m->trace = m->tracefile != NULL;
            } else {
                m->step_mode = 1; m->step_count = 1;
                m->trace = 1; if (!m->tracefile) m->tracefile = stdout;
            }
            goto resume;
        }
        case 'c':
            m->trace = (m->tracefile && m->tracefile != stdout);
            goto resume;
        case 'b':
            if (argc > 1) {
                if (dbg_set_break(m, (uint16_t)num(argv[1]))) puts("too many breakpoints");
                break;
            }
            /* fall through */
        case 'B': {
            int i;
            for (i = 0; i < m->nbrk; i++) printf("%d: %04X\n", i, m->brk[i]);
            break;
        }
        case 'd': {
            int i = (int)num(argv[1]);
            if (argc > 1 && i >= 0 && i < m->nbrk) { memmove(&m->brk[i], &m->brk[i + 1], (m->nbrk - i - 1) * sizeof(uint16_t)); m->nbrk--; }
            break;
        }
        case 'x':
            if (argv[0][1] == 'i') {
                uint16_t a = argc > 1 ? (uint16_t)num(argv[1]) : c->pc;
                disasm_range(m, a, argc > 2 ? (int)num(argv[2]) : 8);
            } else if (argv[0][1] == 'p') {
                uint32_t pa = (uint32_t)(num(argv[1]) * 0x2000 + (argc > 2 ? num(argv[2]) : 0));
                dump_mem(m, 0, argc > 3 ? (int)num(argv[3]) : 64, 1, pa);
            } else {
                if (argc > 1) last_addr = (uint16_t)num(argv[1]);
                int n = argc > 2 ? (int)num(argv[2]) : 64;
                dump_mem(m, last_addr, n, 0, 0);
                last_addr = (uint16_t)(last_addr + n);
            }
            break;
        case 'm':
            if (!strcmp(argv[0], "mmu") && m->level2) {
                int i;
                printf("task0:");
                for (i = 0; i < 8; i++) printf(" %02X", machine_peek_phys(m, 0x70000 + 0xffa0 + i));
                printf("\ntask1:");
                for (i = 0; i < 8; i++) printf(" %02X", machine_peek_phys(m, 0x70000 + 0xffa8 + i));
                printf("\n");
            }
            break;
        case 'w':
            if (argc > 2) machine_write(m, (uint16_t)num(argv[1]), (uint8_t)num(argv[2]));
            break;
        case 't':
            if (argc > 1 && !strcmp(argv[1], "off")) m->trace = 0;
            else {
                m->trace = 1;
                if (argc > 2) { m->tracefile = fopen(argv[2], "w"); }
                else if (!m->tracefile) m->tracefile = stdout;
            }
            break;
        case '0': case '1': {
            int d = argv[0][0] - '0';
            if (argc > 1) {
                if (m->disk[d]) fclose(m->disk[d]);
                m->disk[d] = fopen(argv[1], "r+");
                if (!m->disk[d]) perror(argv[1]);
            }
            break;
        }
        case 'L':
            if (argc > 1) { if (logfile) fclose(logfile); logfile = fopen(argv[1], "w"); }
            break;
        case 'S':
            if (argc > 1 && console_set_inputfile(argv[1])) perror(argv[1]);
            break;
        case 'U':
            if (argc > 1) printf("%d bytes loaded\n", load_srec(m, argv[1]));
            break;
        case 'D':
            if (argc > 3) save_srec(m, argv[1], (uint16_t)num(argv[2]), (uint32_t)num(argv[3]));
            break;
        case 'R':
            machine_reset(m);
            break;
        default:
            printf("unknown command, h for help\n");
        }
    }
resume:
    skip_brk_pc = c->pc;
    console_cooked_end();
    return;
out_quit:
    console_cooked_end();
}
