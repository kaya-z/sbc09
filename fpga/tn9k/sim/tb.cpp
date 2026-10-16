// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// Verilator test bench for the sim09 FPGA system: a console on the UART, a microSD card in SPI mode
// (image file), a script of expect / send lines, power-off detection.
//
//   tb [-d disk0.img] [-d1 disk1.img] [-sdsc] [-s script] [-c clks] [-q] [-t vcd] [-nosd]
//
// Script lines:  expect TEXT   wait until TEXT has been printed (since the last expect)
//                send TEXT     type TEXT (\r is a carriage return)
//                snap FILE     (HDMI build) write the next complete frame of the HDMI output as a PPM picture
//                wait N        N milliseconds of simulated time (at 67.5 MHz)
//                quit          stop
// Exit code: 0 power off ($E00F = $A5) or quit, 1 time out, 2 script expect failed

#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "Vsim_top.h"
#include "verilated.h"
#if VM_TRACE
#include "verilated_vcd_c.h"
#endif

static const int BAUD_DIV = 586;

// ------------------------------------------------------------------ the microSD card (SPI mode 0)
struct SdCard {
    FILE *f[2] = {nullptr, nullptr};
    bool present = true, sdhc = true;
    uint32_t drive1_blk = 0x40000;
    // wire level
    bool last_sck = false, last_cs = true;
    int rxbits = 0; uint8_t rxsh = 0;
    uint8_t txsh = 0xff; int txleft = 0;
    std::vector<uint8_t> outq;                  // bytes to send after the one that is coming now
    // protocol level
    enum { CMD, WTOKEN, WDATA, WCRC } mode = CMD;
    uint8_t cmd[6]; int ncmd = 0;
    bool idle = true, acmd = false, v2 = false;
    int acmd41_tries = 0;
    uint32_t wblk = 0; std::vector<uint8_t> wbuf;
    long reads = 0, writes = 0, cmds = 0;
    int miso = 1;

    void queue(std::initializer_list<uint8_t> l) { for (auto b : l) outq.push_back(b); }

    FILE *file_for(uint32_t blk, long &off) {
        uint32_t b = blk;
        int d = 0;
        if (b >= drive1_blk) { d = 1; b -= drive1_blk; }
        off = (long)b * 512;
        return f[d];
    }

    void handle_cmd() {
        cmds++;
        int c = cmd[0] & 0x3f;
        uint32_t arg = (cmd[1] << 24) | (cmd[2] << 16) | (cmd[3] << 8) | cmd[4];
        bool was_acmd = acmd; acmd = false;
        if (was_acmd && c == 41) {
            if (++acmd41_tries >= 3) { idle = false; queue({0x00}); } else queue({0x01});
            if (arg & 0x40000000) v2 = true;
            return;
        }
        switch (c) {
        case 0: idle = true; acmd41_tries = 0; queue({0x01}); break;
        case 8: if (v2cap) queue({0x01, 0x00, 0x00, (uint8_t)((arg >> 8) & 0xf), (uint8_t)arg}); else queue({0x05}); break;
        case 55: acmd = true; queue({(uint8_t)(idle ? 0x01 : 0x00)}); break;
        case 58: queue({0x00, (uint8_t)(sdhc ? 0xc0 : 0x80), 0xff, 0x80, 0x00}); break;
        case 16: queue({0x00}); break;
        case 17: {
            uint32_t blk = sdhc ? arg : arg >> 9;
            if (!sdhc && (arg & 511)) { queue({0x04}); break; }
            long off; FILE *fp = file_for(blk, off);
            queue({0x00, 0xff});
            uint8_t buf[512]; memset(buf, 0, sizeof buf);
            if (fp) { fseek(fp, off, SEEK_SET); size_t n = fread(buf, 1, 512, fp); (void)n; }
            queue({0xfe});
            for (int i = 0; i < 512; i++) outq.push_back(buf[i]);
            queue({0x12, 0x34});                // CRC (not checked)
            reads++;
            break;
        }
        case 24:
            wblk = sdhc ? arg : arg >> 9;
            queue({0x00});
            mode = WTOKEN;
            break;
        default: queue({0x04}); break;
        }
    }
    bool v2cap = true;

    void got_byte(uint8_t b) {
        // after each received byte the next byte to send is taken from the queue
        switch (mode) {
        case CMD:
            if (ncmd == 0 && (b & 0xc0) != 0x40) break;      // not a command start
            cmd[ncmd++] = b;
            if (ncmd == 6) { ncmd = 0; handle_cmd(); }
            break;
        case WTOKEN:
            if (b == 0xfe) { mode = WDATA; wbuf.clear(); }
            break;
        case WDATA:
            wbuf.push_back(b);
            if (wbuf.size() == 512) mode = WCRC;
            break;
        case WCRC:
            wbuf.push_back(b);
            if (wbuf.size() == 514) {
                long off; FILE *fp = file_for(wblk, off);
                if (fp) { fseek(fp, off, SEEK_SET); fwrite(wbuf.data(), 1, 512, fp); fflush(fp); }
                writes++;
                queue({0x05, 0x00, 0x00, 0x00, 0xff});    // data accepted, busy for 3 bytes
                mode = CMD;
            }
            break;
        }
        if (!outq.empty()) { txsh = outq.front(); outq.erase(outq.begin()); } else txsh = 0xff;
        txleft = 8;
    }

    // called every clock with the pins; returns MISO
    int step(int sck, int cs, int mosi) {
        if (!present) return 1;
        if (cs) {                       // deselected: the transfer is dropped
            if (!last_cs) { mode = (mode == WCRC || mode == WDATA || mode == WTOKEN) ? CMD : mode; ncmd = 0; outq.clear(); }
            rxbits = 0; txleft = 0; miso = 1; last_cs = true; last_sck = sck;
            return 1;
        }
        if (last_cs) { rxbits = 0; txsh = 0xff; txleft = 8; ncmd = 0; }
        last_cs = false;
        if (sck && !last_sck) {         // rising edge: sample MOSI
            rxsh = (uint8_t)((rxsh << 1) | (mosi & 1));
            if (++rxbits == 8) { rxbits = 0; got_byte(rxsh); }
        } else if (!sck && last_sck) {  // falling edge: the next bit out
            if (txleft > 0) { miso = (txsh >> 7) & 1; txsh <<= 1; txleft--; }
            else miso = 1;
        }
        last_sck = sck;
        return miso;
    }
    void start_edge_fix() {}
};

// ------------------------------------------------------------------ the SPI flash with the kanji image (READ 03h, mode 0)
struct Flash {
    std::vector<uint8_t> img;                   // the image, at flash offset 0x100000
    std::vector<uint8_t> img2;                  // the ROM image of the Level 2 machine, at flash offset 0x140000
    bool last_clk = false, last_cs = true;
    int bits = 0, phase = 0, nrx = 0, txleft = 0, miso = 1;
    uint8_t rx = 0, txsh = 0xff;
    uint32_t addr = 0;
    long bytes_read = 0;
    uint8_t at(uint32_t a) {
        if (a >= 0x140000 && a - 0x140000 < img2.size()) return img2[a - 0x140000];
        return (a >= 0x100000 && a - 0x100000 < img.size()) ? img[a - 0x100000] : 0xff;
    }
    int step(int cs, int clk, int mosi) {
        if (cs) { bits = 0; phase = 0; nrx = 0; txleft = 0; miso = 1; last_cs = true; last_clk = clk; return 1; }
        if (last_cs) { bits = 0; phase = 0; nrx = 0; txleft = 0; }
        last_cs = false;
        if (clk && !last_clk) {
            rx = (uint8_t)((rx << 1) | (mosi & 1));
            if (++bits == 8) {
                bits = 0;
                if (phase == 0) { phase = (rx == 0x03) ? 1 : 3; nrx = 0; addr = 0; }
                else if (phase == 1) { addr = (addr << 8) | rx; if (++nrx == 3) { phase = 2; txleft = 0; } }
            }
        } else if (!clk && last_clk) {
            if (phase == 2) {
                if (txleft == 0) { txsh = at(addr++); txleft = 8; bytes_read++; }
                miso = (txsh >> 7) & 1; txsh <<= 1; txleft--;
            } else miso = 1;
        }
        last_clk = clk;
        return miso;
    }
};

// ------------------------------------------------------------------ the console on the UART
struct Uart {
    // receive the FPGA's txd
    int rstate = 0, rcnt = 0, rbit = 0; uint8_t rsh = 0;
    std::string out;
    void rx(int txd, bool echo) {
        if (rstate == 0) { if (!txd) { rstate = 1; rcnt = BAUD_DIV + BAUD_DIV / 2; rbit = 0; rsh = 0; } }
        else if (--rcnt == 0) {
            rcnt = BAUD_DIV;
            if (rbit < 8) { rsh = (uint8_t)((rsh >> 1) | (txd ? 0x80 : 0)); rbit++; }
            else { rstate = 0; out.push_back((char)rsh); if (echo) { fputc(rsh, stdout); fflush(stdout); } }
        }
    }
    // drive the FPGA's rxd
    std::string txq; int tcnt = 0, tbit = 0; uint8_t tsh = 0; int line = 1; bool busy = false;
    void tx() {
        if (!busy) {
            line = 1;
            if (!txq.empty()) { tsh = (uint8_t)txq[0]; txq.erase(0, 1); busy = true; tcnt = BAUD_DIV; tbit = 0; line = 0; }
            return;
        }
        if (--tcnt != 0) return;
        tcnt = BAUD_DIV;
        tbit++;
        if (tbit <= 8) line = (tsh >> (tbit - 1)) & 1;     // data bits, LSB first
        else if (tbit == 9) line = 1;                      // stop bit
        else { busy = false; line = 1; }                   // one more bit time of rest between characters
    }
};

#ifdef HDMI
// ------------------------------------------------------------------ the HDMI output, decoded like a monitor would
static const uint64_t T81 = 14815, TPIX = 39683;        // ps: 67.5 MHz and 25.2 MHz
struct Chan {
    int sum = 0, worst = 0;
    int decode(unsigned w) {                            // 0..255 data, 256+c control, -1 not a TMDS symbol
        static const unsigned ctl[4] = {0x354, 0x0AB, 0x154, 0x2AB};
        for (int i = 0; i < 4; i++) if (w == ctl[i]) { sum = 0; return 256 + i; }
        int n1 = __builtin_popcount(w);
        sum += n1 - (10 - n1);
        if (abs(sum) > worst) worst = abs(sum);
        unsigned b = (w >> 9 & 1) ? (~w & 0xff) : (w & 0xff);
        unsigned d = b & 1;
        for (int i = 1; i < 8; i++) {
            unsigned bi = (b >> i) & 1, bp = (b >> (i - 1)) & 1;
            d |= (((w & 0x100) ? (bi ^ bp) : (~(bi ^ bp) & 1)) & 1) << i;
        }
        return (int)d;
    }
};
struct Video {
    Chan ch[3];
    std::vector<uint8_t> img = std::vector<uint8_t>(640 * 480 * 3), last = std::vector<uint8_t>(640 * 480 * 3);
    int x = 0, y = 0; bool in_frame = false, prev_vs = true, de_prev = false;
    long frames = 0, bad = 0;
    void pixel(Vsim_top *t) {
        int vb = ch[0].decode(t->tmds_b), vg = ch[1].decode(t->tmds_g), vr = ch[2].decode(t->tmds_r);
        if (vb < 0 || vg < 0 || vr < 0) bad++;
        bool de = (vb >= 0 && vb < 256) && vg < 256 && vr < 256;
        bool vs = true;
        if (vb >= 256) vs = (vb - 256) >> 1 & 1;
        if (de) {
            if (in_frame && y < 480 && x < 640) { uint8_t *p = &img[(y * 640 + x) * 3]; p[0] = (uint8_t)vr; p[1] = (uint8_t)vg; p[2] = (uint8_t)vb; }
            x++;
        } else if (de_prev) { y++; x = 0; }
        de_prev = de;
        if (prev_vs && !vs) {
            if (in_frame && y >= 480) { last = img; frames++; }
            in_frame = true; y = 0; x = 0; de_prev = false;
        }
        prev_vs = vs;
    }
    int worst() { return std::max(ch[0].worst, std::max(ch[1].worst, ch[2].worst)); }
};
#endif

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    const char *d0 = nullptr, *d1 = nullptr, *script = nullptr, *vcd = nullptr;
    long maxclk = 4000000000L;
    bool quiet = false, sdsc = false, nosd = false;
    const char *kanji_file = nullptr;
    const char *l2img_file = nullptr;
    long pcevery = 0;
    unsigned pswatch_lo = 0, pswatch_hi = 0; int pswatch_n = 0;       // -ps LO HI: log the PSRAM accesses of the machine to an address range
    long hstart = -1, hlast = -1; std::string hkey; std::map<std::string, std::pair<long, long>> hist;   // -h CLK: clocks per bus cycle from CLK on
    FILE *ctrace = nullptr; long climit = 0; bool cy_pend = false; unsigned cy_addr = 0; int cy_rnw = 0, cy_vma = 0;
    FILE *itrace = nullptr; bool last_lic = true; long ilimit = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc) d0 = argv[++i];
        else if (!strcmp(argv[i], "-d1") && i + 1 < argc) d1 = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) maxclk = atol(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) vcd = argv[++i];
        else if (!strcmp(argv[i], "-q")) quiet = true;
        else if (!strcmp(argv[i], "-sdsc")) sdsc = true;
        else if (!strcmp(argv[i], "-nosd")) nosd = true;
        else if (!strcmp(argv[i], "-kanji") && i + 1 < argc) kanji_file = argv[++i];
        else if (!strcmp(argv[i], "-l2img") && i + 1 < argc) l2img_file = argv[++i];
        else if (!strcmp(argv[i], "-ps") && i + 2 < argc) { pswatch_lo = strtoul(argv[i + 1], 0, 16); pswatch_hi = strtoul(argv[i + 2], 0, 16); i += 2; }
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) hstart = atol(argv[++i]);
        else if (!strcmp(argv[i], "-cy") && i + 2 < argc) { ctrace = fopen(argv[i + 1], "w"); climit = atol(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) pcevery = atol(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i + 2 < argc) { itrace = fopen(argv[i + 1], "w"); ilimit = atol(argv[i + 2]); i += 2; }
    }
    Flash flash;
    if (l2img_file) {
        FILE *lf = fopen(l2img_file, "rb");
        if (!lf) { perror(l2img_file); return 3; }
        int ch;
        while ((ch = fgetc(lf)) != EOF) flash.img2.push_back((uint8_t)ch);
        fclose(lf);
    }
    if (kanji_file) {
        FILE *kf = fopen(kanji_file, "rb");
        if (!kf) { perror(kanji_file); return 3; }
        int ch;
        while ((ch = fgetc(kf)) != EOF) flash.img.push_back((uint8_t)ch);
        fclose(kf);
    }
    SdCard sd;
    if (d0) sd.f[0] = fopen(d0, "r+b");
    if (d1) sd.f[1] = fopen(d1, "r+b");
    sd.sdhc = !sdsc; sd.present = !nosd;

    std::vector<std::pair<std::string, std::string>> steps;      // (verb, arg)
    if (script) {
        FILE *sf = fopen(script, "r");
        if (!sf) { perror(script); return 3; }
        char line[1024];
        while (fgets(line, sizeof line, sf)) {
            size_t n = strlen(line); while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
            char *sp = strchr(line, ' ');
            std::string verb = sp ? std::string(line, sp - line) : std::string(line);
            std::string arg = sp ? sp + 1 : "";
            std::string a2;
            for (size_t i = 0; i < arg.size(); i++) {
                if (arg[i] == '\\' && i + 1 < arg.size() && arg[i+1] == 'r') { a2 += '\r'; i++; } else a2 += arg[i];
            }
            if (!verb.empty()) steps.push_back({verb, a2});
        }
        fclose(sf);
    }

    Vsim_top *top = new Vsim_top;
#ifdef HDMI
    Video vid; uint64_t nextsys = T81 / 2, nextpix = TPIX / 2; int sysl = 0, pixl = 0; long snap_frames = -1;
#endif
#if VM_TRACE
    Verilated::traceEverOn(true);
    VerilatedVcdC *tfp = nullptr;
    if (vcd) { tfp = new VerilatedVcdC; top->trace(tfp, 99); tfp->open(vcd); }
#else
    (void)vcd;
#endif
    Uart u;
    size_t step = 0; size_t scan = 0; long waituntil = -1;
    int rc = 0; bool powered_off = false;
    top->resetn = 0; top->rxd = 1; top->sd_miso = 1; top->fl_miso = 1; top->clk = 0;
    for (int i = 0; i < 8; i++) { top->clk = 0; top->eval(); top->clk = 1; top->eval(); }
    top->resetn = 1;
    long clk = 0;
    long cycles_e = 0; int last_e = -1;
    for (; clk < maxclk; clk++) {
#ifdef HDMI
        for (;;) {                                     // up to the next rising edge of the system clock; the pixel clock runs in between
            if (nextsys <= nextpix) { sysl ^= 1; top->clk = sysl; nextsys += T81 / 2; top->eval(); if (sysl) break; }
            else { pixl ^= 1; top->clk_pix = pixl; nextpix += TPIX / 2; top->eval(); if (pixl) vid.pixel(top); }
        }
#else
        top->clk = 0; top->eval();
        top->clk = 1; top->eval();
#endif
#if VM_TRACE
        if (tfp) tfp->dump(clk);
#endif
        if (ctrace) {                                  // one line per bus cycle: address, direction, valid, data
            if (cy_pend) { fprintf(ctrace, "%04X %c %d %02X\n", cy_addr, cy_rnw ? 'R' : 'W', cy_vma, top->dbg_din); cy_pend = false; if (--climit <= 0) break; }
            if (top->dbg_acc) { cy_pend = true; cy_addr = top->dbg_addr; cy_rnw = top->dbg_rnw; cy_vma = top->dbg_vma; }
        }
        if (pswatch_lo && top->dbg_ps_rd && top->dbg_ps_addr >= pswatch_lo && top->dbg_ps_addr <= pswatch_hi && pswatch_n < 60) {
            pswatch_n++;
            fprintf(stderr, "[ps %s %06X at clk %ld]\n", top->dbg_ps_wr ? "W" : "R", (unsigned)top->dbg_ps_addr, clk);
        }
        if (hstart >= 0 && top->dbg_acc) {              // the length of a bus cycle in system clocks, by kind of cycle
            if (hlast >= 0 && clk >= hstart) { auto &h = hist[hkey]; h.first++; h.second += clk - hlast; }
            hlast = clk;
            unsigned ad = top->dbg_addr;
            hkey = !top->dbg_vma ? "idle (no access)" : (ad >= 0xE000 && ad < 0xE200) ? "I/O page" : ad >= 0xFF00 ? "ROM" : "RAM";
            if (top->dbg_vma) hkey += top->dbg_rnw ? " read" : " write";
        }
        if (itrace && top->dbg_acc) {                 // the address of every instruction fetch
            if (last_lic) { fprintf(itrace, "%04X\n", top->dbg_addr); if (--ilimit <= 0) break; }
            last_lic = top->dbg_lic;
        }
        if (pcevery && clk % pcevery == 0) {
            unsigned pc = top->dbg_regs[3] & 0xffff, sp = top->dbg_regs[1] >> 16, cc = (top->dbg_regs[2] >> 16) & 0xff;
            fprintf(stderr, "[clk %ld pc=%04x s=%04x cc=%02x]\n", clk, pc, sp, cc);
        }
        top->fl_miso = flash.step(top->fl_cs, top->fl_clk, top->fl_mosi);
        top->sd_miso = sd.step(top->sd_clk, top->sd_cs, top->sd_mosi);
        u.rx(top->txd, !quiet);
        u.tx(); top->rxd = u.line;
        if (!(top->led & 0x20)) { rc = 0; powered_off = true; fprintf(stderr, "\n[power off at clk %ld]\n", clk); break; }
        if ((clk & 0xfff) == 0 && step < steps.size()) {
            const auto &s = steps[step];
            if (s.first == "expect") {
                size_t p = u.out.find(s.second, scan);
                if (p != std::string::npos) { scan = p + s.second.size(); step++; }
            } else if (s.first == "send") { u.txq += s.second; step++; }
#ifdef HDMI
            else if (s.first == "snap") {
                if (snap_frames < 0) snap_frames = vid.frames + 2;          // a frame that started after the request
                else if (vid.frames >= snap_frames) {
                    FILE *pf = fopen(s.second.c_str(), "wb");
                    if (pf) { fprintf(pf, "P6\n640 480\n255\n"); fwrite(vid.last.data(), 1, vid.last.size(), pf); fclose(pf); }
                    snap_frames = -1; step++;
                }
            }
#endif
            else if (s.first == "wait") {
                if (waituntil < 0) waituntil = clk + atol(s.second.c_str()) * 67500;
                else if (clk >= waituntil) { waituntil = -1; step++; }
            } else if (s.first == "quit") { rc = 0; break; }
            else step++;
        }
        (void)cycles_e; (void)last_e;
    }
    if (clk >= maxclk) {
        rc = 1;
        fprintf(stderr, "\n[time out at clk %ld, script step %zu/%zu]\n", clk, step, steps.size());
        if (step < steps.size()) fprintf(stderr, "[waiting for: %s %s]\n", steps[step].first.c_str(), steps[step].second.c_str());
    }
#ifdef HDMI
    fprintf(stderr, "[hdmi: %ld frames, %ld invalid symbols, worst DC imbalance %d bits]\n", vid.frames, vid.bad, vid.worst());
#endif
    if (kanji_file) fprintf(stderr, "[flash: %ld bytes read, kanji image loaded: %d]\n", flash.bytes_read, (int)top->kanji_ok);
    fprintf(stderr, "[sd: %ld commands, %ld block reads, %ld block writes; %ld clks]\n", sd.cmds, sd.reads, sd.writes, clk);
    if (step < steps.size() && rc == 0 && !powered_off && steps[step].first != "quit") rc = 2;
#if VM_TRACE
    if (tfp) { tfp->close(); }
#endif
    delete top;
    if (hstart >= 0) {
        long n = 0, t = 0;
        for (auto &h : hist) { printf("%-20s %9ld cycles  %6.2f clocks each\n", h.first.c_str(), h.second.first, (double)h.second.second / h.second.first); n += h.second.first; t += h.second.second; }
        if (n) printf("%-20s %9ld cycles  %6.2f clocks each = E %.3f MHz (67.5 MHz system clock)\n", "all", n, (double)t / n, 67.5 * n / t);
    }
    return rc;
}
