/* SPDX-License-Identifier: MIT */
/*
 * dis6809.c - MC6809 disassembler with OS-9 system call annotation
 *
 * Credits: mnemonics, indexed addressing syntax and the OS-9 service code
 * names follow sbc09 src/d09.c (Shinji Kono's OS-9 aware disassembler,
 * GPL v2) and the OS-9 system call definitions of the NitrOS-9 project.
 * Table driven, written independently.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "m6809.h"
#include "dbg.h"

enum { M_INH, M_IMM8, M_IMM16, M_DIR, M_EXT, M_IDX, M_REL8, M_REL16, M_REGS, M_PSHS, M_PSHU, M_ILL };

static const char *grp0[16] = { "NEG", 0, 0, "COM", "LSR", 0, "ROR", "ASR", "ASL", "ROL", "DEC", 0, "INC", "TST", "JMP", "CLR" };
static const char *grpA[16] = { "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA", "STA", "EORA", "ADCA", "ORA", "ADDA", "CMPX", "JSR", "LDX", "STX" };
static const char *grpB[16] = { "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB", "STB", "EORB", "ADCB", "ORB", "ADDB", "LDD", "STD", "LDU", "STU" };
static const char *brn[16] = { "BRA", "BRN", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ", "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE" };
static const char *regname[16] = { "D", "X", "Y", "U", "S", "PC", "?", "?", "A", "B", "CC", "DP", "?", "?", "?", "?" };
static const char *ixr[4] = { "X", "Y", "U", "S" };

#include "os9names.h"

const char *os9_svc_name(int code)
{
    int i;
    for (i = 0; os9_names[i].code >= 0; i++)
        if (os9_names[i].code == code) return os9_names[i].name;
    return NULL;
}

typedef uint8_t (*rdfn)(void *, uint16_t);

static int idx_text(char *out, size_t sz, uint16_t pc, rdfn rd, void *ctx, uint16_t *np)
{
    uint8_t p = rd(ctx, pc++);
    const char *r = ixr[(p >> 5) & 3];
    int ind = p & 0x10;
    char t[48];
    int ok = 1;

    if (!(p & 0x80)) {
        int o = p & 0x1f;
        if (o & 0x10) o -= 0x20;
        snprintf(t, sizeof t, "%d,%s", o, r);
        goto done;
    }
    switch (p & 0x0f) {
    case 0: snprintf(t, sizeof t, ",%s+", r); ok = !ind; break;
    case 1: snprintf(t, sizeof t, ",%s++", r); break;
    case 2: snprintf(t, sizeof t, ",-%s", r); ok = !ind; break;
    case 3: snprintf(t, sizeof t, ",--%s", r); break;
    case 4: snprintf(t, sizeof t, ",%s", r); break;
    case 5: snprintf(t, sizeof t, "B,%s", r); break;
    case 6: snprintf(t, sizeof t, "A,%s", r); break;
    case 8: { int8_t o = (int8_t)rd(ctx, pc++); snprintf(t, sizeof t, "%d,%s", o, r); break; }
    case 9: { uint16_t o = (uint16_t)(rd(ctx, pc) << 8 | rd(ctx, pc + 1)); pc += 2; snprintf(t, sizeof t, "$%04X,%s", o, r); break; }
    case 11: snprintf(t, sizeof t, "D,%s", r); break;
    case 12: { int8_t o = (int8_t)rd(ctx, pc++); snprintf(t, sizeof t, "%d,PCR", o); break; }
    case 13: { uint16_t o = (uint16_t)(rd(ctx, pc) << 8 | rd(ctx, pc + 1)); pc += 2; snprintf(t, sizeof t, "$%04X,PCR", o); break; }
    case 15:
        if (ind) { uint16_t a = (uint16_t)(rd(ctx, pc) << 8 | rd(ctx, pc + 1)); pc += 2; snprintf(t, sizeof t, "$%04X", a); }
        else { snprintf(t, sizeof t, "?"); ok = 0; }
        break;
    default: snprintf(t, sizeof t, "?"); ok = 0; break;
    }
done:
    if (ind && (p & 0x80) && ok) snprintf(out, sz, "[%s]", t);
    else snprintf(out, sz, "%s", t);
    *np = pc;
    return ok;
}

static void pshlist(char *out, size_t sz, uint8_t m, int isu)
{
    static const char *n[8] = { "CC", "A", "B", "DP", "X", "Y", "?", "PC" };
    int first = 1, i;
    out[0] = 0;
    for (i = 0; i < 8; i++) {
        if (!(m & (1 << i))) continue;
        const char *nm = (i == 6) ? (isu ? "S" : "U") : n[i];
        if (!first) strncat(out, ",", sz - strlen(out) - 1);
        strncat(out, nm, sz - strlen(out) - 1);
        first = 0;
    }
}

int m6809_disasm(char *buf, int bufsz, uint16_t pc0, rdfn rd, void *ctx)
{
    uint16_t pc = pc0;
    uint8_t op = rd(ctx, pc++);
    int pfx = 0;
    const char *name = NULL;
    int mode = M_ILL;
    char opnd[64] = "";

    if (op == 0x10 || op == 0x11) { pfx = op; op = rd(ctx, pc++); }

    if (!pfx) {
        if (op < 0x10 || (op >= 0x60 && op < 0x80)) {
            name = grp0[op & 15];
            mode = (op < 0x10) ? M_DIR : (op < 0x70) ? M_IDX : M_EXT;
            if (!name) mode = M_ILL;
        } else if (op >= 0x40 && op < 0x60) {
            static const char *u[16] = { "NEG", 0, 0, "COM", "LSR", 0, "ROR", "ASR", "ASL", "ROL", "DEC", 0, "INC", "TST", 0, "CLR" };
            static char tmp[8];
            if (u[op & 15]) { snprintf(tmp, sizeof tmp, "%s%c", u[op & 15], op < 0x50 ? 'A' : 'B'); name = tmp; mode = M_INH; }
        } else if (op >= 0x20 && op < 0x30) { name = brn[op & 15]; mode = M_REL8; }
        else if (op >= 0x80) {
            int m = (op >> 4) & 3;
            name = (op & 0x40) ? grpB[op & 15] : grpA[op & 15];
            mode = m == 0 ? M_IMM8 : m == 1 ? M_DIR : m == 2 ? M_IDX : M_EXT;
            if (m == 0) {
                int lo = op & 15;
                int w = (op & 0x40) ? (lo == 3 || lo == 12 || lo == 13 || lo == 14 || lo == 15)
                                    : (lo == 3 || lo == 12 || lo == 14 || lo == 15);
                if (w) mode = M_IMM16;
                if (!(op & 0x40) && lo == 13) { name = "BSR"; mode = M_REL8; }
            }
        } else switch (op) {
            case 0x12: name = "NOP"; mode = M_INH; break;
            case 0x13: name = "SYNC"; mode = M_INH; break;
            case 0x16: name = "LBRA"; mode = M_REL16; break;
            case 0x17: name = "LBSR"; mode = M_REL16; break;
            case 0x19: name = "DAA"; mode = M_INH; break;
            case 0x1a: name = "ORCC"; mode = M_IMM8; break;
            case 0x1c: name = "ANDCC"; mode = M_IMM8; break;
            case 0x1d: name = "SEX"; mode = M_INH; break;
            case 0x1e: name = "EXG"; mode = M_REGS; break;
            case 0x1f: name = "TFR"; mode = M_REGS; break;
            case 0x30: name = "LEAX"; mode = M_IDX; break;
            case 0x31: name = "LEAY"; mode = M_IDX; break;
            case 0x32: name = "LEAS"; mode = M_IDX; break;
            case 0x33: name = "LEAU"; mode = M_IDX; break;
            case 0x34: name = "PSHS"; mode = M_PSHS; break;
            case 0x35: name = "PULS"; mode = M_PSHS; break;
            case 0x36: name = "PSHU"; mode = M_PSHU; break;
            case 0x37: name = "PULU"; mode = M_PSHU; break;
            case 0x39: name = "RTS"; mode = M_INH; break;
            case 0x3a: name = "ABX"; mode = M_INH; break;
            case 0x3b: name = "RTI"; mode = M_INH; break;
            case 0x3c: name = "CWAI"; mode = M_IMM8; break;
            case 0x3d: name = "MUL"; mode = M_INH; break;
            case 0x3f: name = "SWI"; mode = M_INH; break;
            }
    } else if (pfx == 0x10 && op >= 0x20 && op < 0x30) {
        static char tmp[8];
        snprintf(tmp, sizeof tmp, "L%s", brn[op & 15]);
        name = tmp; mode = M_REL16;
    } else if (op == 0x3f) {
        name = pfx == 0x10 ? "SWI2" : "SWI3"; mode = M_INH;
    } else if (op >= 0x80) {
        int m = (op >> 4) & 3, lo = op & 15;
        mode = m == 0 ? M_IMM16 : m == 1 ? M_DIR : m == 2 ? M_IDX : M_EXT;
        if (pfx == 0x10) {
            if (!(op & 0x40)) {
                if (lo == 3) name = "CMPD"; else if (lo == 12) name = "CMPY";
                else if (lo == 14) name = "LDY"; else if (lo == 15) name = "STY";
            } else {
                if (lo == 14) name = "LDS"; else if (lo == 15) name = "STS";
            }
        } else if (!(op & 0x40)) {
            if (lo == 3) name = "CMPU"; else if (lo == 12) name = "CMPS";
        }
        if (!name) mode = M_ILL;
    }

    if (!name || mode == M_ILL) {
        snprintf(buf, bufsz, "FCB     $%02X%s", pfx ? pfx : op, pfx ? "" : "");
        return pfx ? 1 : 1;
    }

    switch (mode) {
    case M_INH: break;
    case M_IMM8: snprintf(opnd, sizeof opnd, "#$%02X", rd(ctx, pc)); pc++; break;
    case M_IMM16: snprintf(opnd, sizeof opnd, "#$%04X", rd(ctx, pc) << 8 | rd(ctx, pc + 1)); pc += 2; break;
    case M_DIR: snprintf(opnd, sizeof opnd, "<$%02X", rd(ctx, pc)); pc++; break;
    case M_EXT: snprintf(opnd, sizeof opnd, "$%04X", rd(ctx, pc) << 8 | rd(ctx, pc + 1)); pc += 2; break;
    case M_IDX: idx_text(opnd, sizeof opnd, pc, rd, ctx, &pc); break;
    case M_REL8: { int8_t o = (int8_t)rd(ctx, pc); pc++; snprintf(opnd, sizeof opnd, "$%04X", (uint16_t)(pc + o)); break; }
    case M_REL16: { int16_t o = (int16_t)(rd(ctx, pc) << 8 | rd(ctx, pc + 1)); pc += 2; snprintf(opnd, sizeof opnd, "$%04X", (uint16_t)(pc + o)); break; }
    case M_REGS: { uint8_t p = rd(ctx, pc++); snprintf(opnd, sizeof opnd, "%s,%s", regname[p >> 4], regname[p & 15]); break; }
    case M_PSHS: pshlist(opnd, sizeof opnd, rd(ctx, pc++), 0); break;
    case M_PSHU: pshlist(opnd, sizeof opnd, rd(ctx, pc++), 1); break;
    }
    /* OS-9 system call: SWI2 followed by the service code byte */
    if (pfx == 0x10 && op == 0x3f) {
        uint8_t code = rd(ctx, pc++);
        const char *n = os9_svc_name(code);
        if (n) snprintf(buf, bufsz, "OS9     %s", n);
        else snprintf(buf, bufsz, "OS9     $%02X", code);
        return pc - pc0;
    }
    snprintf(buf, bufsz, "%-7s %s", name, opnd);
    return pc - pc0;
}
