// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// Verilator test bench for the HDMI text screen: sends console bytes, runs two clocks (67.5 MHz and 25.2 MHz),
// decodes the TMDS symbols like a monitor would, checks their DC balance, and writes a frame as a PPM picture.
//
//   tb_hdmi -i FILE [-o out.ppm] [-f frames] [-g gap]
//     FILE   the bytes to send (a text file with \e, \r, \n, \xHH escapes), sent one every GAP system clocks
//     -f     number of complete frames to run after the bytes are in (default 3); the last one is written
//     -g     GAP (default 150)
//   exit code 0 ok, 1 TMDS error (balance, bad symbol), 2 usage

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "Vhdmi_top.h"
#include "verilated.h"

static const uint64_t T81 = 14815, TPIX = 39683;        // ps: 67.5 MHz, 25.2 MHz

struct Chan {
    int ones_minus_zeros = 0, worst = 0;
    // returns: 0..255 data, 256+c control c, -1 invalid
    int decode(unsigned w) {
        static const unsigned ctl[4] = {0x354, 0x0AB, 0x154, 0x2AB};     // 1101010100 0010101011 0101010100 1010101011
        int n1 = __builtin_popcount(w);
        for (int i = 0; i < 4; i++) if (w == ctl[i]) { ones_minus_zeros = 0; return 256 + i; }
        ones_minus_zeros += n1 - (10 - n1);
        if (abs(ones_minus_zeros) > worst) worst = abs(ones_minus_zeros);
        unsigned qm = (w & 0x100) ? ((w >> 9 & 1) ? (~w & 0xff) : (w & 0xff)) : ((w >> 9 & 1) ? (~w & 0xff) : (w & 0xff));
        // w[9]=1: the 8 low bits were inverted;  w[8]=1: the word was made by XOR, 0: by XNOR
        unsigned b = (w >> 9 & 1) ? (~w & 0xff) : (w & 0xff);
        unsigned d = b & 1;
        for (int i = 1; i < 8; i++) {
            unsigned bi = (b >> i) & 1, bp = (b >> (i - 1)) & 1;
            unsigned x = (w & 0x100) ? (bi ^ bp) : (~(bi ^ bp) & 1);
            d |= x << i;
        }
        (void)qm;
        return (int)d;
    }
};

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    const char *in = nullptr, *out = nullptr;
    int frames = 3, gap = 150;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc) in = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) gap = atoi(argv[++i]);
    }
    if (!in) { fprintf(stderr, "usage: tb_hdmi -i FILE [-o out.ppm] [-f frames] [-g gap]\n"); return 2; }
    std::vector<uint8_t> bytes;
    {
        FILE *f = fopen(in, "rb");
        if (!f) { perror(in); return 2; }
        int c;
        std::string s;
        while ((c = fgetc(f)) != EOF) s.push_back((char)c);
        fclose(f);
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char n = s[++i];
                if (n == 'e') bytes.push_back(27);
                else if (n == 'r') bytes.push_back(13);
                else if (n == 'n') bytes.push_back(10);
                else if (n == 't') bytes.push_back(9);
                else if (n == 'b') bytes.push_back(8);
                else if (n == 'f') bytes.push_back(12);
                else if (n == 'x' && i + 2 < s.size()) { bytes.push_back((uint8_t)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
                else bytes.push_back((uint8_t)n);
            } else if (s[i] != '\n' || 1) {
                if (s[i] == '\n' && i + 1 == s.size()) continue;      // the final newline of the file
                bytes.push_back((uint8_t)s[i]);
            }
        }
    }

    Vhdmi_top *top = new Vhdmi_top;
    top->clk = 0; top->clk_pix = 0; top->rst = 1; top->stb = 0; top->din = 0;
    uint64_t t = 0, nextsys = T81 / 2, nextpix = TPIX / 2;
    int sysl = 0, pixl = 0;
    size_t bi = 0; long sysclk = 0, gapcnt = 0;
    Chan ch[3];
    int err = 0;
    std::vector<uint8_t> img(640 * 480 * 3, 0), last(640 * 480 * 3, 0);
    int x = 0, y = 0; bool in_frame = false, prev_vs = true; int frames_done = 0; int frames_after = -1;
    long bad = 0;
    // the pixel pipeline: the symbols of one clock are those of the previous video position (registered), the
    // sync is in the blue channel's control symbol; a frame starts at the falling edge of vsync
    int vsync_seen = 0;
    bool de_prev = false;
    for (;;) {
        // next edge
        if (nextsys <= nextpix) {
            t = nextsys; sysl ^= 1; top->clk = sysl; nextsys += T81 / 2;
            top->eval();
            if (sysl) {
                sysclk++;
                if (sysclk == 40) top->rst = 0;
                top->stb = 0;
                if (sysclk > 400 && bi < bytes.size() && !top->busy && ++gapcnt >= gap) { gapcnt = 0; top->stb = 1; top->din = bytes[bi++]; if (bi == bytes.size()) frames_after = 0; }
            }
        } else {
            t = nextpix; pixl ^= 1; top->clk_pix = pixl; nextpix += TPIX / 2;
            top->eval();
            if (pixl) {
                int vb = ch[0].decode(top->tmds_b), vg = ch[1].decode(top->tmds_g), vr = ch[2].decode(top->tmds_r);
                if (vb < 0 || vg < 0 || vr < 0) { bad++; }
                bool de = (vb >= 0 && vb < 256) && (vg < 256) && (vr < 256);
                bool vs = true, hs = true;
                if (vb >= 256) { int c = vb - 256; hs = c & 1; vs = (c >> 1) & 1; }
                if (de) {
                    if (in_frame && y >= 0 && y < 480 && x < 640) {
                        uint8_t *p = &img[(y * 640 + x) * 3];
                        p[0] = (uint8_t)vr; p[1] = (uint8_t)vg; p[2] = (uint8_t)vb;
                    }
                    x++;
                } else {
                    if (de_prev) { y++; x = 0; }
                }
                de_prev = de;
                if (prev_vs && !vs) {                      // the frame ends / begins
                    if (in_frame && y >= 480) { last = img; frames_done++; if (frames_after >= 0) frames_after++; }
                    in_frame = true; y = 0; x = 0; de_prev = false;
                    vsync_seen++;
                }
                prev_vs = vs;
                (void)hs;
                if (frames_after >= frames + 1) break;
            }
        }
        if (t > 4000000000ULL * 1000) { fprintf(stderr, "time out\n"); err = 1; break; }
    }
    int worst = std::max(ch[0].worst, std::max(ch[1].worst, ch[2].worst));
    fprintf(stderr, "[%d frames, %ld invalid symbols, worst DC imbalance %d bits, %zu bytes sent]\n", frames_done, bad, worst, bytes.size());
    if (bad || worst > 12) err = 1;
    if (out) {
        FILE *f = fopen(out, "wb");
        fprintf(f, "P6\n640 480\n255\n");
        fwrite(last.data(), 1, last.size(), f);
        fclose(f);
    }
    delete top;
    return err;
}
