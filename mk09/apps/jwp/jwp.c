/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 YUKARI Semiconductor Devices
 *
 * jwp - Japanese word processor for OS-9 (NitrOS-9 Level 1 / Level 2)
 *
 * Part of the MC6809/OS-9 simulator (see README, docs/JWP.md).
 * License: MIT (see LICENSES.md).  The compiled module contains the runtime library
 * of CMOC (GPL v3, Pierre Sarrazin, http://sarrazip.com/dev/cmoc.html) and is therefore
 * under the GNU General Public License version 3; see CREDITS.md.
 * Written in C for CMOC (--os9); runs on any NitrOS-9 6809 system, the
 * simulator being the intended host.
 *
 * Usage: jwp [-v] [-w cols] [-h rows] [-e width] [file]
 *        jwp -v prints the version and the copyright and exits
 *
 * A full screen editor with soft word wrap that understands the multibyte
 * Japanese encodings of the console (UTF-8, EUC-JP, Shift_JIS; taken from
 * PD.PAR of the keyboard path, i.e. `kmode`):
 *   - cursor movement, deletion and display widths are per character,
 *     full width characters take two columns;
 *   - lines wrap at the window width with Japanese line breaking rules
 *     (kinsoku: no closing punctuation at the start of a line, no opening
 *     brackets at its end) and at spaces between latin words;
 *   - the text is kept in memory as a gap buffer, files are read and written
 *     with the line end convention of the file (CR = OS-9, LF, CRLF);
 *   - search, kill/yank, formatted export (hard wrapped copy for printing).
 *
 * The text buffer is 9000 bytes (Level 1 has about 44K of free RAM, the module takes 29K); the Level 2 build
 * (make -C os9/jwp jwp-l2, -DBUFSZ=26000) uses a bigger one.
 * Keys: see the help line, docs/JWP.md.
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "msgs.h"

/* The version: shown by `jwp -v` and kept in the program as a string.  Keep it equal to JWP_VERSION of os9/jwp/jwp.c. */
#define JWP_VERSION   "1.0"
static const char jwp_ident[] = "jwp " JWP_VERSION " (c) 2026 YUKARI Semiconductor Devices";

#ifndef BUFSZ
#define BUFSZ   9000       /* text buffer: what fits next to the module in a Level 1 system */
#endif
#define KBSZ    300
#define ROWMAX  28
#define COLMAX  90
#define OBSZ    (COLMAX * 3 + 40)

typedef unsigned char uchar;

/* ---------------------------------------------------------------- OS-9 calls */

/* ------------------------------------------------------- MK09 system calls */

static int ioerr;                  /* last error code (E_*) */

static int sc_read(int path, uchar *b, int n)
{
    int r = read(path, b, n);
    if (r < 0) { ioerr = -r; return -1; }
    return r;
}

static int sc_write(int path, uchar *b, int n)
{
    int r = write(path, b, n);
    if (r < 0) { ioerr = -r; return -1; }
    return r;
}

/* mode 1: read, 2: read and write (existing file) */
static int sc_open(char *name, int mode)
{
    int r = open(name, mode == 1 ? FO_READ : FO_READ | FO_WRITE);
    if (r < 0) { ioerr = -r; return -1; }
    return r;
}

static int sc_create(char *name)
{
    int r = open(name, FO_WRITE | FO_CREATE | FO_TRUNC);
    if (r < 0) { ioerr = -r; return -1; }
    return r;
}

static void sc_close(int path)
{
    close(path);
}

static int sc_delete(char *name)
{
    int r = unlink(name);
    if (r < 0) { ioerr = -r; return -1; }
    return 0;
}

static unsigned g_hi, g_lo;        /* results of sc_size */

/* the position hi:lo (32 bit) */
static int sc_seek(int path, unsigned hi, unsigned lo)
{
    int r = seek(path, hi, lo, 0, 0, 0);
    if (r < 0) { ioerr = -r; return -1; }
    return 0;
}

/* the file size in g_hi:g_lo */
static int sc_size(int path)
{
    mkstat_t st;
    int r = fstat(path, &st);
    if (r < 0) { ioerr = -r; return -1; }
    g_hi = st.sizehi;
    g_lo = st.sizelo;
    return 0;
}

/* number of characters waiting on the keyboard */
static int sc_ready(void)
{
    int r = tty_ready();
    return r > 0 ? r : 0;
}


/* ------------------------------------------------------------------- state */

static uchar buf[BUFSZ];          /* gap buffer: text = buf[0..gs) + buf[ge..BUFSZ) */
static int gs, ge;
#define enc JWP_ENC                /* the character set (1 UTF-8, 2 EUC-JP, 3 Shift_JIS): fixed when the program is built */
static int cols = 80, rows = 24, trows, ewid = 72;
static int top;                   /* first text index shown (always a visual line start) */
static int gcol, gvalid;          /* goal column for vertical moves */
static int modified, eolmode, truncated;   /* eolmode: 0 CR, 1 LF, 2 CRLF */
static char fname[96];
static uchar pn[100];
static uchar kbuf[KBSZ];
static int klen, lastkill;
static char pat[48];
static const char *msgp;
static const char *M[NMSG];       /* the texts (filled from a string blob at the start) */
static const unsigned *ks_nostart, *ks_noend;
static int ks_ns_n, ks_ne_n;
static const char *tabstr;
static u8 savemode, saveenc, rawset;
static int skk_mode;               /* SKK input mode: 0 latin, 1 hiragana, 2 katakana */

/* ------------------------------------------------------- text / characters */

static int tlen(void) { return gs + BUFSZ - ge; }

#define AT(i) ((i) < gs ? buf[(i)] : buf[(i) + (ge - gs)])

static int at(int i)
{
    return AT(i);
}

/* word wise block copies (memmove of the CMOC library is a byte loop) */
static void fwdcopy(uchar *d, uchar *sr, int n)
{
    asm {
        pshs y,u
        ldx  :sr
        ldy  :n
        ldu  :d
        tfr  y,d
        bitb #1
        beq  fcev
        lda  ,x+
        sta  ,u+
fcev    tfr  y,d
        lsra
        rorb
        tfr  d,y
        cmpy #0
        beq  fcend
fcloop  ldd  ,x++
        std  ,u++
        leay -1,y
        bne  fcloop
fcend   puls y,u
    }
}

/* CR -> LF in place */
static void cr2lf(uchar *p, int n)
{
    asm {
        pshs y
        ldx  :p
        ldy  :n
        beq  c2end
c2loop  lda  ,x+
        cmpa #13
        bne  c2nx
        lda  #10
        sta  -1,x
c2nx    leay -1,y
        bne  c2loop
c2end   puls y
    }
}

static void bwdcopy(uchar *d, uchar *sr, int n)
{
    asm {
        pshs y,u
        ldx  :sr
        ldd  :n
        leax d,x
        ldu  :d
        leau d,u
        tfr  d,y
        bitb #1
        beq  bcev
        lda  ,-x
        sta  ,-u
bcev    tfr  y,d
        lsra
        rorb
        tfr  d,y
        cmpy #0
        beq  bcend
bcloop  ldd  ,--x
        std  ,--u
        leay -1,y
        bne  bcloop
bcend   puls y,u
    }
}

/* unicode display width (East Asian Wide / Fullwidth = 2) */
static int uw(unsigned cp)
{
    if (cp < 0x1100) return 1;
    if (cp <= 0x115f) return 2;
    if (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) return 2;
    if (cp >= 0xac00 && cp <= 0xd7a3) return 2;
    if (cp >= 0xf900 && cp <= 0xfaff) return 2;
    if (cp >= 0xfe30 && cp <= 0xfe6f) return 2;
    if (cp >= 0xff00 && cp <= 0xff60) return 2;
    if (cp >= 0xffe0 && cp <= 0xffe6) return 2;
    return 1;
}

/* Length in bytes, display width and code of the character at p (rem bytes
 * are available).  code 0xffff marks an invalid byte. */
static int sinfo(const uchar *p, int rem, int *w, unsigned *code)
{
    unsigned b = p[0], b2;
    int l, k;
    *w = 1;
    *code = b;
    if (b < 0x80 || enc == 0) return 1;
    if (enc == 1) {
        if (b >= 0xe0 && b < 0xf0 && rem >= 3 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
            b2 = ((b & 0x0f) << 12) | ((unsigned)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
            *code = b2;
            *w = (b2 >= 0x3000 && b2 < 0xa000) ? 2 : uw(b2);
            return 3;
        }
        if (b >= 0xc2 && b < 0xe0) l = 2;
        else if (b >= 0xe0 && b < 0xf0) l = 3;
        else if (b >= 0xf0 && b < 0xf5) l = 4;
        else { *code = 0xffff; return 1; }
        if (l > rem) { *code = 0xffff; return 1; }
        for (k = 1; k < l; k++)
            if ((p[k] & 0xc0) != 0x80) { *code = 0xffff; return 1; }
        if (l == 2) *code = ((b & 0x1f) << 6) | (p[1] & 0x3f);
        else if (l == 3) *code = ((b & 0x0f) << 12) | ((unsigned)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        else { *code = 0xfffe; *w = 2; return 4; }
        *w = uw(*code);
        return l;
    }
    if (enc == 2) {
        if (b == 0x8e) {
            if (rem >= 2 && p[1] >= 0xa1) { *code = 0x8e00 | p[1]; return 2; }
        } else if (b == 0x8f) {
            if (rem >= 3 && p[1] >= 0xa1 && p[2] >= 0xa1) { *code = 0; *w = 2; return 3; }
        } else if (b >= 0xa1 && b <= 0xfe) {
            b2 = p[1];
            if (rem >= 2 && b2 >= 0xa1 && b2 <= 0xfe) { *code = (b << 8) | b2; *w = 2; return 2; }
        }
        *code = 0xffff;
        return 1;
    }
    /* Shift_JIS */
    if ((b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc)) {
        b2 = p[1];
        if (rem >= 2 && b2 >= 0x40 && b2 != 0x7f && b2 <= 0xfc) { *code = (b << 8) | b2; *w = 2; return 2; }
        *code = 0xffff;
        return 1;
    }
    if (b >= 0xa1 && b <= 0xdf) return 1;
    *code = 0xffff;
    return 1;
}

static int cinfo(int i, int *w, unsigned *code)
{
    uchar t[4], *p;
    int rem, k;
    if (i < gs) {
        p = buf + i;
        rem = gs - i;
        if (rem < 4 && ge != BUFSZ) {      /* the character may continue behind the gap */
            rem = tlen() - i;
            if (rem > 4) rem = 4;
            for (k = 0; k < rem; k++) t[k] = AT(i + k);
            return sinfo(t, rem, w, code);
        }
    } else {
        p = buf + i + (ge - gs);
        rem = BUFSZ - (i + (ge - gs));
    }
    return sinfo(p, rem, w, code);
}

static unsigned ccode(int i)
{
    int w;
    unsigned c;
    if (i >= tlen()) return 0;
    cinfo(i, &w, &c);
    return c;
}

/* start of the character that ends at i */
static int prevc(int i)
{
    int p, q, l, w;
    unsigned c;
    if (i <= 0) return 0;
    if (enc == 0) return i - 1;
    if (enc == 1) {
        p = i - 1;
        while (p > 0 && i - p < 4 && (at(p) & 0xc0) == 0x80) p--;
        l = cinfo(p, &w, &c);
        return p + l == i ? p : i - 1;
    }
    p = i - 1;
    while (p > 0 && at(p - 1) != '\n') p--;
    q = p;
    for (;;) {
        l = cinfo(q, &w, &c);
        if (q + l >= i) break;
        q += l;
    }
    return q;
}

static int isbound(int i)
{
    int p = i, q, w;
    unsigned c;
    if (enc == 0 || i <= 0 || i >= tlen()) return 1;
    if (enc == 1) return (at(i) & 0xc0) != 0x80;
    while (p > 0 && at(p - 1) != '\n') p--;
    q = p;
    while (q < i) q += cinfo(q, &w, &c);
    return q == i;
}

static int inset(const unsigned *t, int n, unsigned c)
{
    int i;
    if (enc == 0 && c >= 0x80) return 0;
    for (i = 0; i < n; i++)
        if (t[i] == c) return 1;
    return 0;
}

static int isal(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* ------------------------------------------------------------ gap buffer */

static void movegap(int p)
{
    int k;
    if (p < gs) {
        k = gs - p;
        bwdcopy(buf + ge - k, buf + p, k);
        ge -= k;
        gs = p;
    } else if (p > gs) {
        k = p - gs;
        fwdcopy(buf + gs, buf + ge, k);
        ge += k;
        gs = p;
    }
}

static void touch(int p, int d);

static void ins(const uchar *s, int n)
{
    if (ge - gs < n) { msgp = M[M_FULL]; return; }
    touch(gs, n);
    while (n-- > 0) buf[gs++] = *s++;
    modified = 1;
}

/* -------------------------------------------------------------- layout */

static int lb_hard, lb_reach;     /* lb_reach: end of the text the decision depended on */
static int cs[COLMAX + 4];       /* start indexes of the characters of the line being laid out */

/* length and width of the character at i (text length n) without its code;
 * the common double byte Japanese characters are recognized directly */
static int clen(int i, int n, int *w)
{
    int b = AT(i);
    unsigned c;
    if (b < 0x80 || !enc) { *w = 1; return 1; }
    if (enc == 1) {
        if (b >= 0xe3 && b <= 0xe9 && i + 2 < n && (AT(i + 1) & 0xc0) == 0x80 && (AT(i + 2) & 0xc0) == 0x80) {
            *w = 2;
            return 3;
        }
    } else if (enc == 2) {
        if (b >= 0xa1 && b <= 0xfe && i + 1 < n && AT(i + 1) >= 0xa1) { *w = 2; return 2; }
    }
    return cinfo(i, w, &c);
}

/* End (exclusive) of the visual line that starts at s; lb_hard is set when the
 * line ends at a newline (which is included). */
static int lbreak(int s)
{
    int n = tlen(), i = s, k = 0, col = 0, l, w, c, brk, j, push;
    lb_hard = 0;
    for (;;) {
        if (i >= n) { lb_reach = n; return n; }
        c = AT(i);
        if (c == '\n') { lb_hard = 1; lb_reach = i + 1; return i + 1; }
        l = clen(i, n, &w);
        if (col + w > cols) {
            lb_reach = i + l;
            if (c == ' ') { i++; continue; }         /* spaces hang over the margin */
            brk = i;
            if (k > 1 && isal(c) && isal(AT(cs[k - 1]))) {
                for (j = k - 1; j > 0; j--)
                    if (AT(cs[j]) == ' ') break;
                if (AT(cs[j]) == ' ') brk = cs[j] + 1;
            } else {
                push = 0;
                while (k > 1 && push < 3 &&
                       (inset(ks_nostart, ks_ns_n, ccode(brk)) || inset(ks_noend, ks_ne_n, ccode(cs[k - 1])))) {
                    brk = cs[--k];
                    push++;
                }
            }
            return brk;
        }
        if (k < COLMAX + 2) cs[k++] = i;
        col += w;
        i += l;
    }
}

/* start of the visual line containing index i */
static int lstart(int i)
{
    int n = tlen(), p, e;
    if (i > n) i = n;
    p = i;
    while (p > 0 && at(p - 1) != '\n') p--;
    for (;;) {
        e = lbreak(p);
        if (e > i || (e >= n && !lb_hard)) return p;
        p = e;
    }
}

static int linecol(int i, int s)
{
    int col = 0, w;
    unsigned c;
    while (s < i) { s += cinfo(s, &w, &c); col += w; }
    return col;
}

/* index of the character covering column `want` of the line [s,e) */
static int idxcol(int s, int e, int hard, int want)
{
    int n = tlen(), col = 0, w, l, last = s, lim = hard ? e - 1 : e;
    unsigned c;
    while (s < lim) {
        l = cinfo(s, &w, &c);
        if (col + w > want) return s;
        col += w;
        last = s;
        s += l;
    }
    if (!hard && e < n) return last;
    return s;
}

/* ------------------------------------------------------------- terminal */

static uchar ob[OBSZ];
static int on, hstart, ocol;
static unsigned rh1[ROWMAX], rh2[ROWMAX];
static uchar rvalid[ROWMAX];

static void oc(int c) { if (on < OBSZ - 8) ob[on++] = c; }

static void onum(unsigned v)
{
    char t[6];
    int k = 0;
    do { t[k++] = '0' + v % 10; v /= 10; } while (v);
    while (k) oc(t[--k]);
}

static void rowbegin(int r)
{
    on = 0;
    oc(27); oc('['); onum(r + 1); oc(';'); oc('1'); oc('H');
    oc(27); oc('['); oc('K');
    hstart = on;
    ocol = 0;
}

static void rowend(int r)
{
    unsigned h1 = 0, h2 = 0;
    int k;
    for (k = hstart; k < on; k++) {
        h1 = ((h1 << 5) ^ (h1 >> 11)) + ob[k];
        h2 += ob[k] ^ (unsigned)k;
    }
    if (rvalid[r] && rh1[r] == h1 && rh2[r] == h2) return;
    rvalid[r] = 1;
    rh1[r] = h1;
    rh2[r] = h2;
    sc_write(1, ob, on);
}

/* append a C string in the guest encoding, clipped at the window width */
static void ostrcol(const char *s)
{
    int rem = strlen(s), l, w, k;
    unsigned code;
    while (rem > 0) {
        if ((uchar)*s < 0x80 || !enc) { l = 1; w = 1; }
        else l = sinfo((const uchar *)s, rem, &w, &code);
        if (ocol + w > cols) break;
        for (k = 0; k < l; k++) oc(s[k]);
        ocol += w;
        s += l;
        rem -= l;
    }
}

static void opad(void)
{
    while (ocol < cols) { oc(' '); ocol++; }
}

static char *numstr(unsigned v, char *d)
{
    char t[6];
    int k = 0;
    do { t[k++] = '0' + v % 10; v /= 10; } while (v);
    while (k) *d++ = t[--k];
    *d = 0;
    return d;
}

static void cat(char *d, const char *s)
{
    strcat(d, s);
}

static void statusline(int col)
{
    char st[220], *p;
    int rl;
    st[0] = 0;
    cat(st, fname[0] ? fname : M[M_NONAME]);
    if (modified) { cat(st, " "); cat(st, M[M_MODIFIED]); }
    cat(st, "  ");
    cat(st, M[M_COL]);
    p = st + strlen(st);
    p = numstr(col + 1, p);
    cat(st, "  ");
    cat(st, M[M_POS]);
    p = st + strlen(st);
    p = numstr(gs, p);
    *p++ = '/';
    p = numstr(tlen(), p);
    *p = 0;
    cat(st, eolmode == 0 ? "  CR" : eolmode == 1 ? "  LF" : "  CRLF");
    cat(st, enc == 0 ? " 8bit" : enc == 1 ? " UTF-8" : enc == 2 ? " EUC-JP" : " SJIS");
    if (enc) {
        cat(st, " ");
        cat(st, skk_mode == 1 ? M[M_KANA] : skk_mode == 2 ? M[M_KATA] : M[M_LATIN]);
    }
    rl = trows;
    rowbegin(rl);
    oc(27); oc('['); oc('7'); oc('m');
    ostrcol(st);
    opad();
    oc(27); oc('['); oc('m');
    rowend(rl);
}

static const char *lastm;

static void msgline(const char *s)
{
    if (rvalid[rows - 1] && s == lastm) return;
    lastm = s;
    rowbegin(rows - 1);
    ostrcol(s);
    rowend(rows - 1);
}

/* layout cache: the visual lines of the window.  Rows below lcn are valid.  An
 * edit at index p that inserts (d > 0) or removes (d < 0) bytes invalidates the
 * rows that end near or after p (touch()).  When the layout is redone after a
 * single edit, it stops as soon as a new line starts where an old line (shifted
 * by d) started: the rest of the window is unchanged.  plan() then decides which
 * rows have to be drawn again (rdirty), using the terminal's insert / delete line
 * functions when rows only moved. */
static int rs[ROWMAX], re[ROWMAX], rr[ROWMAX];
static uchar rhd[ROWMAX], rfin[ROWMAX], rdirty[ROWMAX];
static int bs[ROWMAX], be[ROWMAX], br[ROWMAX];
static uchar bhd[ROWMAX], bfin[ROWMAX];
static int lcn, topdirty, fullre, ed_cnt, ed_p, ed_d;

static void touch(int p, int d)
{
    int r;
    ed_cnt++;
    ed_p = p;
    ed_d = d;
    if (p <= top + 3) { topdirty = 1; lcn = 0; return; }
    for (r = 0; r < lcn; r++)
        if (rr[r] >= p) { lcn = r; return; }
}

static void layout(void)
{
    int r, s, e, n = tlen(), j, k, lim, shift, r0 = lcn;
    shift = ed_cnt == 1 && !fullre && r0 < trows;
    lim = ed_p + (ed_d < 0 ? -ed_d : 0) + 4;
    if (shift)
        for (r = r0; r < trows; r++) { bs[r] = rs[r]; be[r] = re[r]; br[r] = rr[r]; bhd[r] = rhd[r]; bfin[r] = rfin[r]; }
    for (r = r0; r < trows; r++) {
        if (r == 0) s = top;
        else if (rs[r - 1] < 0 || rfin[r - 1]) { rs[r] = -1; rfin[r] = 1; continue; }
        else s = re[r - 1];
        e = lbreak(s);
        rs[r] = s;
        re[r] = e;
        rr[r] = lb_reach;
        rhd[r] = lb_hard;
        rfin[r] = (e >= n && !lb_hard);
        if (shift && !rfin[r]) {
            for (j = r0 + 1; j < trows; j++)
                if (bs[j] >= lim && bs[j] + ed_d == e) break;
            if (j < trows) {
                for (k = 0; r + 1 + k < trows && j + k < trows; k++) {
                    if (bs[j + k] < 0) { rs[r + 1 + k] = -1; rfin[r + 1 + k] = 1; }
                    else {
                        rs[r + 1 + k] = bs[j + k] + ed_d;
                        re[r + 1 + k] = be[j + k] + ed_d;
                        rr[r + 1 + k] = br[j + k] + ed_d;
                        rhd[r + 1 + k] = bhd[j + k];
                        rfin[r + 1 + k] = bfin[j + k];
                    }
                }
                r += k;
            }
        }
    }
    lcn = trows;
}

#ifdef SELFCHECK
/* test build (make jwp_chk): the cached layout must equal a layout from scratch */
static void verify(void)
{
    static const uchar bugm[] = "LAYOUT-BUG";
    int r, s = top, e, n = tlen(), fin = 0, bad = 0;
    for (r = 0; r < trows; r++) {
        if (fin) { if (rs[r] >= 0) bad = 1; continue; }
        e = lbreak(s);
        if (rs[r] != s || re[r] != e || rhd[r] != lb_hard || rr[r] != lb_reach || rfin[r] != (e >= n && !lb_hard)) bad = 1;
        fin = (e >= n && !lb_hard);
        s = e;
    }
    if (bad) sc_write(1, (uchar *)bugm, 10);
}
#endif

static int findcursor(void)
{
    int r, n = tlen();
    for (r = 0; r < trows; r++) {
        if (rs[r] < 0) break;
        if ((gs >= rs[r] && gs < re[r]) || (rfin[r] && gs == re[r])) return r;
    }
    return -1;
}

/* layout of the rows as displayed on the terminal */
static int ds[ROWMAX], de[ROWMAX], match[ROWMAX];
static uchar dv;

/* insert (d > 0) or delete (d < 0) lines at window row a on the terminal and move the row hashes along */
static void scrollrows(int a, int d)
{
    int r, k = d > 0 ? d : -d;
    if (k > trows - a) k = trows - a;
    on = 0;
    oc(27); oc('['); oc('1'); oc(';'); onum(trows); oc('r');
    oc(27); oc('['); onum(a + 1); oc(';'); oc('1'); oc('H');
    oc(27); oc('['); onum(k); oc(d > 0 ? 'L' : 'M');
    oc(27); oc('['); oc('r');
    sc_write(1, ob, on);
    if (d > 0) {
        for (r = trows - 1; r >= a + k; r--) { rvalid[r] = rvalid[r - k]; rh1[r] = rh1[r - k]; rh2[r] = rh2[r - k]; }
        for (r = a; r < a + k; r++) rvalid[r] = 0;
    } else {
        for (r = a; r + k < trows; r++) { rvalid[r] = rvalid[r + k]; rh1[r] = rh1[r + k]; rh2[r] = rh2[r + k]; }
        for (r = trows - k; r < trows; r++) rvalid[r] = 0;
    }
}

/* Decide which rows must be drawn: a new row that is an unchanged old row (same
 * text) needs no drawing; when such rows only moved up or down the terminal is
 * told to insert / delete lines. */
static void plan(void)
{
    int r, j, lim, delta = 0, a = 0, moved = 0;
    for (r = 0; r < trows; r++) { match[r] = -1; rdirty[r] = 1; }
    if (dv && ed_cnt <= 1) {
        lim = ed_p + (ed_d < 0 ? -ed_d : 0);
        for (r = 0; r < trows; r++) {
            if (rs[r] < 0) { if (ds[r] < 0) match[r] = r; continue; }
            for (j = 0; j < trows; j++) {
                if (ds[j] < 0) continue;
                if ((ed_cnt == 0 || de[j] <= ed_p) && ds[j] == rs[r] && de[j] == re[r]) break;
                if (ed_cnt == 1 && ds[j] >= lim && ds[j] + ed_d == rs[r] && de[j] + ed_d == re[r]) break;
            }
            if (j < trows) match[r] = j;
        }
        for (r = 0; r < trows; r++)
            if (match[r] >= 0 && match[r] != r) {
                delta = r - match[r];
                a = delta > 0 ? match[r] : r;
                moved = 1;
                break;
            }
        if (moved) scrollrows(a, delta);
        for (r = 0; r < trows; r++)
            if (match[r] >= 0 && ((r < a && match[r] == r) || (moved && r >= a && match[r] == r - delta) || (!moved && match[r] == r)))
                rdirty[r] = 0;
    }
}

static void snapshot(void)
{
    int r;
    for (r = 0; r < trows; r++) {
        ds[r] = rs[r];
        de[r] = rs[r] < 0 ? 0 : re[r];
    }
    dv = 1;
}

static void rowcopy(int d, int sr)
{
    rs[d] = rs[sr];
    re[d] = re[sr];
    rr[d] = rr[sr];
    rhd[d] = rhd[sr];
    rfin[d] = rfin[sr];
}

/* put the freshly laid out line [s, e) into row r */
static void rowset(int r, int s, int e)
{
    rs[r] = s;
    re[r] = e;
    rr[r] = lb_reach;
    rhd[r] = lb_hard;
    rfin[r] = (e >= tlen() && !lb_hard);
}

static void redraw(void)
{
    int n = tlen(), r, k, w, l, ccol, crow, cend, s;
    unsigned code;
    int c;

    if (topdirty) { top = lstart(top > n ? n : top); topdirty = 0; lcn = 0; }
    k = 0;
    while (gs < top && lcn == trows && k < 3) {          /* scroll up by whole lines */
        s = lstart(top - 1);
        r = lbreak(s);
        if (r != top) break;
        for (r = trows - 1; r > 0; r--) rowcopy(r, r - 1);
        rowset(0, s, top);
        top = s;
        k++;
    }
    if (gs < top) { top = lstart(gs); lcn = 0; fullre = 1; }
    layout();
    crow = findcursor();
    for (k = 0; crow < 0 && k < 3 && rs[trows - 1] >= 0 && !rfin[trows - 1]; k++) {   /* scroll down by whole lines */
        for (r = 0; r + 1 < trows; r++) rowcopy(r, r + 1);
        top = rs[0];
        s = re[trows - 2];
        rowset(trows - 1, s, lbreak(s));
        crow = findcursor();
    }
    if (crow < 0) {
        s = lstart(gs);
        for (k = 0; k < trows - 1 && s > 0; k++) s = lstart(s - 1);
        top = s;
        lcn = 0;
        fullre = 1;
        layout();
        crow = findcursor();
        if (crow < 0) crow = 0;
    }
#ifdef SELFCHECK
    verify();
#endif
    ccol = linecol(gs, rs[crow]);

    plan();
    ed_cnt = 0;
    fullre = 0;
    for (r = 0; r < trows; r++) {
        if (!rdirty[r]) continue;
        rdirty[r] = 0;
        rowbegin(r);
        if (rs[r] >= 0) {
            cend = rhd[r] ? re[r] - 1 : re[r];
            k = rs[r];
            while (k < cend && ocol < cols) {          /* spaces hanging over the margin are not drawn */
                c = AT(k);
                if (c < 0x80 || !enc) {
                    oc(c < 0x20 || c == 0x7f ? (c == '\t' ? ' ' : '?') : c);
                    k++;
                    ocol++;
                    continue;
                }
                if (enc == 1 && c >= 0xe3 && c <= 0xe9 && k + 2 < cend && (AT(k + 1) & 0xc0) == 0x80 && (AT(k + 2) & 0xc0) == 0x80) {
                    ob[on] = c;
                    ob[on + 1] = AT(k + 1);
                    ob[on + 2] = AT(k + 2);
                    if (on < OBSZ - 12) on += 3;
                    ocol += 2;
                    k += 3;
                    continue;
                }
                l = cinfo(k, &w, &code);
                if (code == 0xffff || (code == 0 && l == 3)) {
                    oc('?');
                } else {
                    for (s = 0; s < l; s++) oc(AT(k + s));
                }
                ocol += w;
                k += l;
            }
        }
        rowend(r);
    }
    snapshot();
    if (ccol >= cols) ccol = cols - 1;
    statusline(ccol);
    msgline(msgp ? msgp : M[M_HELP]);
    on = 0;
    oc(27); oc('['); onum(crow + 1); oc(';'); onum(ccol + 1); oc('H');
    sc_write(1, ob, on);
}

static void invalidate(void)
{
    int r;
    for (r = 0; r < ROWMAX; r++) rvalid[r] = 0;
    lcn = 0;
    fullre = 1;
    dv = 0;
}

static void clearscreen(void)
{
    static const uchar cl[] = { 27, '[', '2', 'J', 27, '[', 'H' };
    sc_write(1, (uchar *)cl, sizeof(cl));
    invalidate();
}

/* ---------------------------------------------------------------- input */

static int rb(void)
{
    uchar c;
    if (sc_read(0, &c, 1) != 1) return -1;
    return c;
}

#define K_NONE   0x100
#define K_UP     0x101
#define K_DOWN   0x102
#define K_LEFT   0x103
#define K_RIGHT  0x104
#define K_HOME   0x105
#define K_END    0x106
#define K_PGUP   0x107
#define K_PGDN   0x108
#define K_DEL    0x109
#define K_TOP    0x10a
#define K_BOTTOM 0x10b
#define K_EOF    0x1ff

static int getkey(void)
{
    int c = rb(), n;
    if (c < 0) return K_EOF;
    if (c != 27) return c;
    c = rb();
    if (c == '[' || c == 'O') {
        c = rb();
        switch (c) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        }
        if (c >= '0' && c <= '9') {
            n = 0;
            while (c >= '0' && c <= '9') { n = n * 10 + c - '0'; c = rb(); }
            switch (n) {
            case 1: case 7: return K_HOME;
            case 4: case 8: return K_END;
            case 3: return K_DEL;
            case 5: return K_PGUP;
            case 6: return K_PGDN;
            }
        }
        return K_NONE;
    }
    if (c == 'v' || c == 'V') return K_PGUP;
    if (c == '<') return K_TOP;
    if (c == '>') return K_BOTTOM;
    return K_NONE;
}

static int needbytes(int b)
{
    if (enc == 1) return b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : b >= 0xc0 ? 2 : 1;
    if (enc == 2) return b == 0x8f ? 3 : (b == 0x8e || b >= 0xa1) ? 2 : 1;
    if (enc == 3) return ((b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc)) ? 2 : 1;
    return 1;
}

static uchar ib[4];

/* read the rest of a multibyte character that starts with `lead` into ib */
static int readmb(int lead)
{
    int l = enc ? needbytes(lead) : 1, k, c;
    ib[0] = lead;
    for (k = 1; k < l; k++) {
        c = rb();
        if (c < 0) return k;
        ib[k] = c;
    }
    return l;
}

/* line input on the message row; returns 1 when accepted */
static int askline_s(const char *prompt, char *dst, int max)
{
    int c, l, w, rem, p;
    unsigned code;
    for (;;) {
        rvalid[rows - 1] = 0;
        rowbegin(rows - 1);
        ostrcol(prompt);
        ostrcol(dst);
        oc(27); oc('['); onum(rows); oc(';'); onum(ocol + 1); oc('H');
        sc_write(1, ob, on);
        c = getkey();
        if (c == '\r' || c == '\n') { rvalid[rows - 1] = 0; return 1; }
        if (c == 7 || c == K_EOF) { rvalid[rows - 1] = 0; return 0; }
        if (c == 8 || c == 0x7f) {
            rem = strlen(dst);
            p = 0;
            l = 0;
            while (p < rem) {
                l = sinfo((uchar *)dst + p, rem - p, &w, &code);
                if (p + l >= rem) break;
                p += l;
            }
            if (rem) dst[p] = 0;
        } else if (c >= 0x20 && c < 0x100 && c != 0x7f) {
            l = readmb(c);
            if ((int)strlen(dst) + l < max) {
                p = strlen(dst);
                memcpy(dst + p, ib, l);
                dst[p + l] = 0;
            }
        }
    }
}

static int askline(int mi, char *dst, int max)
{
    return askline_s(M[mi], dst, max);
}

static int askyn(int mi)
{
    int c;
    rvalid[rows - 1] = 0;
    rowbegin(rows - 1);
    ostrcol(M[mi]);
    sc_write(1, ob, on);
    c = getkey();
    rvalid[rows - 1] = 0;
    return c == 'y' || c == 'Y';
}

/* ---------------------------------------------------------------- files */

static void osname(const char *s)
{
    int k = 0;
    while (s[k] && k < 96) { pn[k] = s[k]; k++; }
    pn[k] = 0;
}

static uchar wb[256];
static int wn, wp, werr;

static void wflush(void)
{
    if (wn && sc_write(wp, wb, wn) < 0 && !werr) werr = ioerr;
    wn = 0;
}

static void wput(int c)
{
    if (wn == sizeof(wb)) wflush();
    wb[wn++] = c;
}

static void weol(void)
{
    if (eolmode == 0) wput('\r');
    else if (eolmode == 1) wput('\n');
    else { wput('\r'); wput('\n'); }
}

/* write a stretch of the text in a one byte line end convention (CR or LF) */
static void wseg(const uchar *p, int n)
{
    uchar *d;
    int m, k;
    while (n > 0) {
        m = n > (int)sizeof(wb) ? (int)sizeof(wb) : n;
        wflush();
        d = wb;
        k = m;
        if (eolmode == 0) {
            while (k-- > 0) { *d = *p++; if (*d == '\n') *d = '\r'; d++; }
        } else {
            while (k-- > 0) *d++ = *p++;
        }
        wn = m;
        n -= m;
    }
}

/* Write the text; ww > 0: formatted export, every visual line (wrapped at ww) becomes a line */
static int savefile(const char *name, int ww)
{
    int p, i, n = tlen(), e, hard, cend, k, savecols = cols;
    osname(name);
    p = sc_open(pn, 1);
    if (p >= 0) {
        sc_close(p);
        if (sc_delete(pn) < 0) return ioerr;
    }
    wp = sc_create(pn);
    if (wp < 0) return ioerr;
    wn = 0;
    werr = 0;
    if (ww > 0) {
        cols = ww;
        i = 0;
        for (;;) {
            e = lbreak(i);
            hard = lb_hard;
            cend = hard ? e - 1 : e;
            for (k = i; k < cend; k++) wput(at(k));
            if (hard || e < n || cend > i) weol();
            if (e >= n && !hard) break;
            i = e;
        }
        cols = savecols;
    } else if (eolmode < 2) {
        wseg(buf, gs);
        wseg(buf + ge, BUFSZ - ge);
        if (n && at(n - 1) != '\n') weol();
    } else {
        for (i = 0; i < n; i++) {
            k = at(i);
            if (k == '\n') weol(); else wput(k);
        }
        if (n && at(n - 1) != '\n') weol();
    }
    wflush();
    sc_close(wp);
    return werr;
}

static int loadfile(const char *name)
{
    int p, n = 0, r, i, want;
    osname(name);
    p = sc_open(pn, 1);
    if (p < 0) return ioerr;
    truncated = 0;
    for (;;) {
        want = BUFSZ - n;
        if (want > 500) want = 500;
        if (want == 0) { truncated = 1; break; }
        r = sc_read(p, buf + n, want);
        if (r <= 0) break;
        n += r;
    }
    sc_close(p);
    /* the first line end decides the convention */
    for (i = 0; i < n && buf[i] != '\r' && buf[i] != '\n'; i++)
        ;
    eolmode = 0;
    if (i < n) {
        uchar *pp = buf + i, *qq = buf + n, *dd;
        if (*pp == '\n') eolmode = 1;
        else if (i + 1 < n && buf[i + 1] == '\n') {
            eolmode = 2;
            for (dd = pp; pp < qq; pp++)
                if (!(*pp == '\r' && pp + 1 < qq && pp[1] == '\n')) *dd++ = *pp;
            n = dd - buf;
        } else {
            cr2lf(pp, qq - pp);
        }
    }
    gs = n;
    ge = BUFSZ;
    movegap(0);
    top = 0;
    lcn = 0;
    fullre = 1;
    modified = 0;
    gvalid = 0;
    return 0;
}

static void errmsg(int e)
{
    static char t[80];
    t[0] = 0;
    cat(t, M[M_ERROR]);
    numstr(e, t + strlen(t));
    msgp = t;
    lastm = 0;
}

/* ------------------------------------------------------------- commands */

static void gotoidx(int p)
{
    movegap(p);
}

static void mvleft(void) { if (gs > 0) gotoidx(prevc(gs)); gvalid = 0; }

static void mvright(void)
{
    int w;
    unsigned c;
    if (gs < tlen()) gotoidx(gs + cinfo(gs, &w, &c));
    gvalid = 0;
}

/* the cursor row of the layout cache, or -1 when the cache cannot be trusted */
static int cachedrow(void)
{
    if (lcn != trows || topdirty) return -1;
    return findcursor();
}

static void setgoal(void)
{
    int r;
    if (gvalid) return;
    r = cachedrow();
    gcol = linecol(gs, r >= 0 ? rs[r] : lstart(gs));
    gvalid = 1;
}

static void mvup(void)
{
    int r, ls, ps, e;
    setgoal();
    r = cachedrow();
    if (r > 0) {
        gotoidx(idxcol(rs[r - 1], re[r - 1], rhd[r - 1], gcol));
        return;
    }
    ls = lstart(gs);
    if (ls == 0) return;
    ps = lstart(ls - 1);
    e = lbreak(ps);
    gotoidx(idxcol(ps, e, lb_hard, gcol));
}

static void mvdown(void)
{
    int r, ls, e, n = tlen(), e2;
    setgoal();
    r = cachedrow();
    if (r >= 0 && r + 1 < trows) {
        if (rs[r + 1] >= 0) gotoidx(idxcol(rs[r + 1], re[r + 1], rhd[r + 1], gcol));
        return;
    }
    ls = lstart(gs);
    e = lbreak(ls);
    if (e >= n && !lb_hard) return;
    e2 = lbreak(e);
    gotoidx(idxcol(e, e2, lb_hard, gcol));
}

static void mvlinestart(void) { gotoidx(lstart(gs)); gvalid = 0; }

static void mvlineend(void)
{
    int ls = lstart(gs), e = lbreak(ls), n = tlen();
    if (lb_hard) gotoidx(e - 1);
    else if (e >= n) gotoidx(n);
    else gotoidx(prevc(e));
    gvalid = 0;
}

static void backspace(void)
{
    int p, k;
    if (gs > 0) {
        k = gs;
        p = prevc(gs);
        gs = p;
        touch(p, p - k);
        modified = 1;
    }
    gvalid = 0;
}

static void delfwd(void)
{
    int w, k;
    unsigned c;
    if (ge < BUFSZ) {
        w = cinfo(gs, &k, &c);
        touch(gs, -w);
        ge += w;
        modified = 1;
    }
    gvalid = 0;
}

static void killline(void)
{
    int n = tlen(), e = gs, k;
    while (e < n && at(e) != '\n') e++;
    if (e == gs && e < n) e++;
    if (e == gs) return;
    if (!lastkill) klen = 0;
    for (k = gs; k < e && klen < KBSZ; k++) kbuf[klen++] = at(k);
    touch(gs, gs - e);
    ge += e - gs;
    modified = 1;
    gvalid = 0;
}

static void dosearch(void)
{
    int n = tlen(), pl = strlen(pat), i, k, w, start;
    unsigned c;
    if (!pl) return;
    start = gs + (gs < n ? cinfo(gs, &w, &c) : 0);
    for (k = 0; k < 2; k++) {
        for (i = k ? 0 : start; i + pl <= n && (k == 0 || i <= gs); i++) {
            if (at(i) != (uchar)pat[0]) continue;
            for (w = 1; w < pl; w++)
                if (at(i + w) != (uchar)pat[w]) break;
            if (w == pl && (((uchar)pat[0] < 0x80 && enc != 3) || isbound(i))) {
                gotoidx(i);
                gvalid = 0;
                return;
            }
        }
    }
    msgp = M[M_NOTFOUND];
}

static void donew(const char *name)
{
    int r;
    strcpy(fname, name);
    r = loadfile(name);
    if (r == 0) msgp = truncated ? M[M_TOOBIG] : M[M_LOADED];
    else if (r == E_NOENT) { gs = 0; ge = BUFSZ; top = 0; lcn = 0; fullre = 1; modified = 0; eolmode = 1; truncated = 0; msgp = M[M_NEW]; }
    else { errmsg(r); gs = 0; ge = BUFSZ; top = 0; lcn = 0; fullre = 1; modified = 0; }
}

static void dosave(int as)
{
    char nm[96];
    int r;
    if (as || !fname[0]) {
        strcpy(nm, fname);
        if (!askline(M_SAVEAS, nm, sizeof(nm) - 1) || !nm[0]) return;
    } else {
        strcpy(nm, fname);
        if (truncated) { msgp = M[M_TOOBIG]; return; }
    }
    r = savefile(nm, 0);
    if (r) { errmsg(r); return; }
    strcpy(fname, nm);
    modified = 0;
    truncated = 0;
    msgp = M[M_SAVED];
}

static void doexport(void)
{
    char nm[96];
    int r;
    nm[0] = 0;
    if (!askline(M_EXPORT, nm, sizeof(nm) - 1) || !nm[0]) return;
    r = savefile(nm, ewid);
    if (r) errmsg(r); else msgp = M[M_EXPORTED];
}

static void doopen(void)
{
    char nm[96];
    if (modified && !askyn(M_DISCARD)) return;
    nm[0] = 0;
    if (!askline(M_OPEN, nm, sizeof(nm) - 1) || !nm[0]) return;
    donew(nm);
}

#include "skk.c"

/* ------------------------------------------------------------ terminal mode */

static void rawmode(void)
{
    u8 mode, cs, cols_, rows_;
    if (tty_getmode(&mode, &cs, &cols_, &rows_) < 0) return;
    savemode = mode;
    tty_setmode(TM_RAW, 0xFF, 0xFF, 0xFF);     /* no echo, no line editing, no ^C, no CR LF translation */
    rawset = 1;
}

static void cleanup(void)
{
    static const uchar fin[] = { 27, '[', 'm' };
    sc_write(1, (uchar *)fin, sizeof(fin));
    rowbegin(rows - 1);
    sc_write(1, ob, on);
    if (rawset) tty_setmode(savemode, 0xFF, 0xFF, 0xFF);
    rawset = 0;
}

/* --------------------------------------------------------------------- main */

static int getnum(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0';
    return v;
}

int main(int argc, char **argv)
{
    int i, c, quit = 0, n, k, w;
    char *a;
    const char *file = 0;

    for (i = 1; i < argc; i++) {
        a = argv[i];
        if (a[0] == '-' && a[1] == 'v' && !a[2]) {
            sc_write(1, (uchar *)jwp_ident, sizeof(jwp_ident) - 1);
            { static uchar eol[2] = {13, 10}; sc_write(1, eol, 2); }
            return 0;
        }
        if (a[0] == '-' && (a[1] == 'd' || a[1] == 'u')) {
            const char *v = a + 2;
            if (!*v && i + 1 < argc) v = argv[++i];
            strncpy(a[1] == 'd' ? dict_name : user_name, v, 62);
        } else if (a[0] == '-' && (a[1] == 'w' || a[1] == 'h' || a[1] == 'e')) {
            const char *v = a + 2;
            if (!*v && i + 1 < argc) v = argv[++i];
            n = getnum(v);
            if (a[1] == 'w' && n >= 20) cols = n;
            else if (a[1] == 'h' && n >= 6) rows = n;
            else if (a[1] == 'e' && n >= 10) ewid = n;
        } else file = a;
    }
    if (cols > COLMAX) cols = COLMAX;
    if (rows > ROWMAX) rows = ROWMAX;
    if (ewid > COLMAX) ewid = COLMAX;
    trows = rows - 2;

    gs = 0;
    ge = BUFSZ;
    {
        u8 mode, cs, cols_, rows_;
        const char *blob = msg_ja;
        int m;
        if (tty_getmode(&mode, &cs, &cols_, &rows_) == 0) {
            if (cs != JWP_ENC) tty_setmode(mode, JWP_ENC, 0xFF, 0xFF);   /* the program, the dictionary and the console agree */
            if (cols == 80 && rows == 24 && cols_ && rows_) { cols = cols_; rows = rows_; trows = rows - 2; }
        }
        for (m = 0; m < NMSG; m++) {
            M[m] = blob;
            blob += strlen(blob) + 1;
        }
        ks_nostart = ks_nostart_ja;
        ks_noend = ks_noend_ja;
        ks_ns_n = sizeof(ks_nostart_ja) / sizeof(unsigned);
        ks_ne_n = sizeof(ks_noend_ja) / sizeof(unsigned);
        tabstr = tab_ja;
    }
    eolmode = 1;

    skk_init();
    if (file) donew(file);
    rawmode();
    clearscreen();
    lastkill = 0;

    while (!quit) {
        if (!sc_ready()) redraw();
        c = getkey();
        msgp = 0;
        if (c == K_EOF) break;
        if (c != 11) lastkill = 0;
        if (skk_key(c)) { gvalid = 0; continue; }
        if (c >= 0x20 && c < 0x7f && c < 0x100) {
            uchar ch = c;
            ins(&ch, 1);
            gvalid = 0;
        } else if (c >= 0x80 && c < 0x100) {
            n = readmb(c);
            ins(ib, n);
            gvalid = 0;
        } else switch (c) {
        case 1: mvlinestart(); break;                   /* ^A */
        case 2: case K_LEFT: mvleft(); break;           /* ^B */
        case 4: case K_DEL: delfwd(); break;            /* ^D */
        case 5: mvlineend(); break;                     /* ^E */
        case 6: case K_RIGHT: mvright(); break;         /* ^F */
        case 7: eolmode = (eolmode + 1) % 3; modified = 1; break;   /* ^G */
        case 8: case 0x7f: backspace(); break;          /* ^H, DEL */
        case 9: { int l = strlen(tabstr); ins((const uchar *)tabstr, l); gvalid = 0; break; }
        case 10: case 13: { uchar nl = '\n'; ins(&nl, 1); gvalid = 0; break; }
        case 11: killline(); lastkill = 1; break;       /* ^K */
        case 12: clearscreen(); break;                  /* ^L */
        case 14: case K_DOWN: mvdown(); break;          /* ^N */
        case 15: doopen(); break;                       /* ^O */
        case 16: case K_UP: mvup(); break;              /* ^P */
        case 17:                                        /* ^Q */
            if (!modified || askyn(M_QUIT)) quit = 1;
            break;
        case 18:                                        /* ^R */
            if (askline(M_SEARCH, pat, sizeof(pat) - 1)) dosearch();
            break;
        case 19: dosave(0); break;                      /* ^S */
        case 20: dosearch(); break;                     /* ^T */
        case 21: case K_PGUP:                           /* ^U */
            for (k = 0; k < trows - 1; k++) mvup();
            break;
        case 22: case K_PGDN:                           /* ^V */
            for (k = 0; k < trows - 1; k++) mvdown();
            break;
        case 23: dosave(1); break;                      /* ^W */
        case 24: doexport(); break;                     /* ^X */
        case 25:                                        /* ^Y */
            ins(kbuf, klen);
            gvalid = 0;
            break;
        case K_HOME: mvlinestart(); break;
        case K_END: mvlineend(); break;
        case K_TOP: gotoidx(0); gvalid = 0; break;
        case K_BOTTOM: gotoidx(tlen()); gvalid = 0; break;
        default: break;
        }
    }
    cleanup();
    return 0;
}
