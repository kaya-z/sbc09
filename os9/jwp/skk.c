/* SPDX-License-Identifier: MIT */
/*
 * skk.c - SKK style kana-kanji conversion for jwp (included by jwp.c)
 *
 * Input in the manner of SKK (Sasaki / Sato et al., the Simple Kana to Kanji conversion
 * program): romaji -> kana, a capital letter starts a reading (marked ▽ in the text), Space
 * converts it (▼ shows the candidate), okurigana are marked with a capital letter.
 *
 *   ^J            latin mode -> hiragana mode (in hiragana mode ^J = Enter)
 *   l             hiragana mode -> latin mode          q   hiragana <-> katakana
 *   Kanji<Space>  ▽かんじ -> ▼漢字     KaKu<Space> -> 書く (reading "かk" with okurigana く)
 *   Space / x     next / previous candidate (from the 5th one: a s d f j k l select)
 *   Enter or any other key   accept the candidate     ^G   cancel
 *   /abc<Space>   abbreviation: the reading is typed as it is
 *   when there is no candidate, the word is asked for and registered in the user dictionary
 *
 * The dictionary is a sorted text file of SKK lines ("よみ /候補/候補/") on disk (tools/mkskkdic.py
 * builds it from SKK-JISYO.L; it is not loaded into memory but searched with a binary search on
 * 256 byte blocks, the way the SKK programs search a sorted dictionary file).  Words that
 * were registered or chosen other than the first candidate are appended to the user dictionary,
 * which is read in full for every conversion and comes first.
 *
 * Part of the MC6809/OS-9 simulator.  MIT License (see LICENSES.md).  The idea and the
 * input method are those of SKK (Masahiko Sato et al.); no SKK code is used.
 */

#define SK_LATIN 0
#define SK_HIRA  1
#define SK_KATA  2
#define K_TSU    0x22
#define K_N      0x52

#define SK_IDLE  0
#define SK_PRE   1
#define SK_SEL   2

#define MAXLN    600
#define MAXCAND  60
#define CBSZ     700

static int sk_state;
static int sk_pre;                 /* index of the marker */
static int sk_rstart;              /* start of the reading (behind the marker) */
static int sk_okpos;               /* index of the '*' okurigana mark, -1 if none */
static int sk_okc;                 /* okurigana consonant (lower case letter) */
static int sk_abbrev, sk_trigger;
static char sk_rb[5];
static int sk_rn;
static uchar sk_rd[64];            /* the reading (kana) as typed */
static int sk_rdn;
static uchar sk_okana[12];         /* okurigana text */
static int sk_okn;
static char sk_cbuf[CBSZ];
static char *sk_cand[MAXCAND];
static int sk_nc, sk_ci, sk_cblen, sk_lbase;
static uchar sk_key[70];
static int sk_keyn;
static char sk_msg[200];
static const uchar *kpunct;
static const char *mk_pre, *mk_sel;
static char dict_name[64] = "/d1/skk.dic";
static char user_name[64] = "/d1/skkuser.dic";

/* ------------------------------------------------------------- dictionary files */

typedef struct {
    int fd;
    unsigned nb;           /* size in blocks */
    unsigned bn;           /* block in b[] */
    int bl;                /* bytes in b[] */
    uchar b[256];
} RD;

static RD rdD;
static int dict_ok;               /* the dictionary file exists */

static int rd_open(RD *r, const char *name)
{
    uchar *p = pn;
    int k = 0;
    r->fd = -1;
    r->bn = 0xffff;
    while (name[k] && k < 96) { *p++ = name[k]; k++; }
    *p = '\r';
    r->fd = sc_open(pn, 1);
    if (r->fd < 0) return 0;
    if (sc_size(r->fd) < 0) { sc_close(r->fd); r->fd = -1; return 0; }
    r->nb = (g_hi >= 256) ? 0xffff : (g_hi << 8) + (g_lo >> 8) + ((g_lo & 255) ? 1 : 0);
    return 1;
}

static void rd_close(RD *r)
{
    if (r->fd >= 0) sc_close(r->fd);
    r->fd = -1;
}

static int rd_blk(RD *r, unsigned n)
{
    int got = 0, m;
    if (r->bn == n) return r->bl;
    if (sc_seek(r->fd, n >> 8, n << 8) < 0) { r->bn = 0xffff; return 0; }
    while (got < 256) {
        m = sc_read(r->fd, r->b + got, 256 - got);
        if (m <= 0) break;
        got += m;
    }
    r->bn = n;
    r->bl = got;
    return got;
}

/* first line that starts in block blk or later */
static int rd_start(RD *r, unsigned blk, unsigned *sb, int *so)
{
    unsigned n;
    int l, i;
    if (blk == 0) { *sb = 0; *so = 0; return rd_blk(r, 0) > 0; }
    if (rd_blk(r, blk - 1) == 256 && r->b[255] == 13) {
        *sb = blk;
        *so = 0;
        return rd_blk(r, blk) > 0;
    }
    for (n = blk; n < r->nb; n++) {
        l = rd_blk(r, n);
        if (l <= 0) return 0;
        for (i = 0; i < l; i++)
            if (r->b[i] == 13) {
                i++;
                if (i < l) { *sb = n; *so = i; return 1; }
                if (rd_blk(r, n + 1) > 0) { *sb = n + 1; *so = 0; return 1; }
                return 0;
            }
    }
    return 0;
}

/* read the line at (b, o) (without its CR) and move behind it; -1 at the end of the file */
static int rd_line(RD *r, unsigned *b, int *o, uchar *dst, int max)
{
    int n = 0, l, c;
    for (;;) {
        l = rd_blk(r, *b);
        if (*o >= l) {
            if (l < 256) return n > 0 ? n : -1;
            (*b)++;
            *o = 0;
            continue;
        }
        c = r->b[(*o)++];
        if (c == 13) return n;
        if (n < max) dst[n++] = c;
    }
}

static int keycmp(const uchar *line, int n, const uchar *key, int klen)
{
    int i;
    for (i = 0; i < klen; i++) {
        if (i >= n || line[i] == ' ') return -1;
        if (line[i] != key[i]) return line[i] < key[i] ? -1 : 1;
    }
    if (klen < n && line[klen] != ' ') return 1;
    return 0;
}

/* ------------------------------------------------------------------- candidates */

static void addcand(const uchar *s, int n)
{
    int i;
    if (n <= 0 || sk_nc >= MAXCAND || sk_cblen + n + 1 > CBSZ) return;
    for (i = 0; i < sk_nc; i++)
        if ((int)strlen(sk_cand[i]) == n && !memcmp(sk_cand[i], s, n)) return;
    memcpy(sk_cbuf + sk_cblen, s, n);
    sk_cbuf[sk_cblen + n] = 0;
    sk_cand[sk_nc++] = sk_cbuf + sk_cblen;
    sk_cblen += n + 1;
}

/* "key /c1/c2;note/c3/": add the candidates of a matching line */
static void parsecands(const uchar *line, int n, int klen)
{
    int i = klen + 1, s, e;
    if (i >= n || line[i] != '/') return;
    i++;
    while (i < n) {
        s = i;
        while (i < n && line[i] != '/') i++;
        e = s;
        while (e < i && line[e] != ';') e++;
        if (e > s && line[s] != '(') addcand(line + s, e - s);
        i++;
    }
}

static uchar sk_line[MAXLN + 4];

static void dict_lookup1(const uchar *key, int klen)
{
    unsigned lo = 0, hi, mid, sb;
    int so, n, cmp;
    RD *r = &rdD;
    dict_ok = rd_open(r, dict_name);
    if (!dict_ok) return;
    hi = r->nb;
    while (hi - lo > 1) {
        mid = lo + (hi - lo) / 2;
        if (!rd_start(r, mid, &sb, &so)) { hi = mid; continue; }
        n = rd_line(r, &sb, &so, sk_line, MAXLN);
        if (n < 0) { hi = mid; continue; }
        cmp = keycmp(sk_line, n, key, klen);
        if (cmp == 0) { parsecands(sk_line, n, klen); return; }
        if (cmp < 0) lo = mid; else hi = mid;
    }
    if (!rd_start(r, lo, &sb, &so)) return;
    for (;;) {
        n = rd_line(r, &sb, &so, sk_line, MAXLN);
        if (n < 0) return;
        cmp = keycmp(sk_line, n, key, klen);
        if (cmp == 0) { parsecands(sk_line, n, klen); return; }
        if (cmp > 0) return;
    }
}

static void dict_lookup(const uchar *key, int klen)
{
    dict_lookup1(key, klen);
    rd_close(&rdD);
}

static void user_lookup(const uchar *key, int klen)
{
    unsigned b = 0;
    int o = 0, n, i, j;
    char *t;
    RD *r = &rdD;
    if (!rd_open(r, user_name)) return;
    for (;;) {
        n = rd_line(r, &b, &o, sk_line, MAXLN);
        if (n < 0) break;
        if (keycmp(sk_line, n, key, klen) == 0) parsecands(sk_line, n, klen);
    }
    rd_close(r);
    for (i = 0, j = sk_nc - 1; i < j; i++, j--) {      /* the latest entry first */
        t = sk_cand[i];
        sk_cand[i] = sk_cand[j];
        sk_cand[j] = t;
    }
}

static void user_add(const uchar *key, int klen, const uchar *cand, int cn)
{
    uchar ln[160];
    int n = 0, k, fd, created = 0;
    uchar *p = pn;
    if (klen + cn + 6 > (int)sizeof(ln)) return;
    for (k = 0; k < klen; k++) ln[n++] = key[k];
    ln[n++] = ' ';
    ln[n++] = '/';
    for (k = 0; k < cn; k++) ln[n++] = cand[k];
    ln[n++] = '/';
    ln[n++] = '\r';
    k = 0;
    while (user_name[k] && k < 96) { *p++ = user_name[k]; k++; }
    *p = '\r';
    fd = sc_open(pn, 2);
    if (fd < 0) { fd = sc_create(pn); created = 1; }
    if (fd < 0) { errmsg(os9err); return; }
    if (!created && sc_size(fd) >= 0) sc_seek(fd, g_hi, g_lo);
    sc_write(fd, ln, n);
    sc_close(fd);
}

/* all candidates of key: the user dictionary first, then the main dictionary */
static void sk_lookup(const uchar *key, int klen)
{
    sk_nc = 0;
    sk_cblen = 0;
    user_lookup(key, klen);
    dict_lookup(key, klen);
}

/* ------------------------------------------------------------------ kana bytes */

/* bytes of kana k (0..0x53 = hiragana from ぁ, 0x60.. = ー 、 。 「 」 ・), hiragana or katakana */
static int kanabytes(int k, int kata, uchar *d)
{
    unsigned cp;
    int n;
    const uchar *p;
    if (k >= 0x60) {
        p = kpunct;
        for (n = k - 0x60; n > 0; n--) { while (*p) p++; p++; }
        n = 0;
        while (*p) d[n++] = *p++;
        return n;
    }
    if (enc == 1) {
        cp = 0x3041 + k + (kata ? 0x60 : 0);
        d[0] = 0xe0 | (cp >> 12);
        d[1] = 0x80 | ((cp >> 6) & 0x3f);
        d[2] = 0x80 | (cp & 0x3f);
        return 3;
    }
    if (enc == 2) {
        d[0] = kata ? 0xa5 : 0xa4;
        d[1] = 0xa1 + k;
        return 2;
    }
    n = kata ? (k < 0x3f ? 0x8340 + k : 0x8341 + k) : 0x829f + k;
    d[0] = n >> 8;
    d[1] = n & 255;
    return 2;
}

/* hiragana -> katakana in the text [a, b) (before the gap, so it is contiguous) */
static void tokata(int a, int b)
{
    int i = a, l, w, k;
    unsigned code;
    uchar t[4];
    while (i < b) {
        l = sinfo(buf + i, b - i, &w, &code);
        if (enc == 1 && code >= 0x3041 && code <= 0x3093) kanabytes(code - 0x3041, 1, buf + i);
        else if (enc == 2 && (code >> 8) == 0xa4 && (code & 255) >= 0xa1 && (code & 255) <= 0xf3) buf[i] = 0xa5;
        else if (enc == 3 && code >= 0x829f && code <= 0x82f1) {
            k = code - 0x829f;
            kanabytes(k, 1, t);
            buf[i] = t[0];
            buf[i + 1] = t[1];
        }
        i += l;
    }
}

/* ---------------------------------------------------------------- buffer helpers */

/* delete [from, cursor) */
static void sk_cut(int from)
{
    if (from >= gs) return;
    touch(from, from - gs);
    gs = from;
    modified = 1;
}

static void sk_put(const uchar *s, int n)
{
    if (n > 0) ins(s, n);
}

static void sk_putc(int c)
{
    uchar ch = c;
    ins(&ch, 1);
}

static void sk_setmsg(void)
{
    int i, k = 0;
    if (sk_rn) {
        for (i = 0; i < sk_rn; i++) sk_msg[k++] = sk_rb[i];
        sk_msg[k] = 0;
        msgp = sk_msg;
        lastm = 0;
    }
}

/* put kana k1 (and k2) at the cursor: into the reading, or as final text */
static void sk_emit(int k1, int k2)
{
    uchar d[8];
    int n, kata = (skk_mode == SK_KATA && sk_state == SK_IDLE);
    n = kanabytes(k1, kata, d);
    sk_put(d, n);
    if (k2 != 255) {
        n = kanabytes(k2, kata, d);
        sk_put(d, n);
    }
    if (sk_okc) sk_trigger = 1;
}

static int rom_find(void)
{
    const char *p = romr;
    int i, len, pre = 0;
    for (i = 0; i < NROM; i++) {
        len = strlen(p);
        if (len >= sk_rn && !memcmp(p, sk_rb, sk_rn)) {
            if (len == sk_rn) return i;
            pre = 1;
        }
        p += len + 1;
    }
    return pre ? -2 : -1;
}

static void sk_shift(void)
{
    int i;
    for (i = 1; i < sk_rn; i++) sk_rb[i - 1] = sk_rb[i];
    sk_rn--;
}

static void sk_romaji(int c)
{
    int r;
    sk_rb[sk_rn++] = c;
    for (;;) {
        r = rom_find();
        if (r >= 0) {
            sk_emit(romk[2 * r], romk[2 * r + 1]);
            sk_rn = 0;
            return;
        }
        if (r == -2) return;
        if (sk_rn == 1) { sk_rn = 0; return; }
        if (sk_rb[0] == 'n') sk_emit(K_N, 255);
        else if (sk_rb[0] == sk_rb[1] && sk_rb[0] != 'a' && sk_rb[0] != 'i' && sk_rb[0] != 'u' && sk_rb[0] != 'e' && sk_rb[0] != 'o')
            sk_emit(K_TSU, 255);
        sk_shift();
    }
}

/* a pending "n" becomes ん */
static void sk_flush(void)
{
    if (sk_rn == 1 && sk_rb[0] == 'n') sk_emit(K_N, 255);
    sk_rn = 0;
}

static void sk_start(int abbrev)
{
    sk_pre = gs;
    sk_put((const uchar *)mk_pre, strlen(mk_pre));
    sk_rstart = gs;
    sk_okpos = -1;
    sk_okc = 0;
    sk_trigger = 0;
    sk_abbrev = abbrev;
    sk_rn = 0;
    sk_state = SK_PRE;
}

/* drop the marker and finish: the reading stays as text */
static void sk_commit_reading(void)
{
    int n, i;
    sk_flush();
    if (sk_okpos >= 0) {                   /* the '*' of an okurigana mark is dropped */
        n = gs - (sk_okpos + 1);
        for (i = 0; i < n; i++) sk_okana[i] = buf[sk_okpos + 1 + i];
        sk_cut(sk_okpos);
        if (n > 0 && n < 12) sk_put(sk_okana, n);
    }
    n = gs - sk_rstart;
    if (n > 0 && n <= 60) {
        memcpy(sk_rd, buf + sk_rstart, n);
        sk_cut(sk_pre);
        if (skk_mode == SK_KATA && !sk_abbrev) {
            sk_put(sk_rd, n);
            tokata(gs - n, gs);
        } else sk_put(sk_rd, n);
    } else sk_cut(sk_pre);
    sk_state = SK_IDLE;
    sk_okpos = -1;
    sk_okc = 0;
}

static void sk_cancel(void)
{
    sk_rn = 0;
    sk_cut(sk_pre);
    sk_state = SK_IDLE;
    sk_okpos = -1;
    sk_okc = 0;
}

/* the text of the marker and the candidate sk_ci */
static void sk_show(void)
{
    const char *cd = sk_cand[sk_ci];
    sk_cut(sk_pre);
    sk_put((const uchar *)mk_sel, strlen(mk_sel));
    sk_put((const uchar *)cd, strlen(cd));
    sk_put(sk_okana, sk_okn);
    if (sk_ci >= 4) {                       /* list of the candidates of this page */
        int i, k = 0, n;
        static const char lab[] = "asdfjkl";
        sk_lbase = 4 + ((sk_ci - 4) / 7) * 7;
        for (i = 0; i < 7 && sk_lbase + i < sk_nc; i++) {
            n = strlen(sk_cand[sk_lbase + i]);
            if (k + n + 4 >= (int)sizeof(sk_msg)) break;
            sk_msg[k++] = lab[i];
            sk_msg[k++] = ':';
            memcpy(sk_msg + k, sk_cand[sk_lbase + i], n);
            k += n;
            sk_msg[k++] = ' ';
        }
        sk_msg[k] = 0;
        msgp = sk_msg;
        lastm = 0;
    }
}

/* restore the reading after a cancelled conversion */
static void sk_back(void)
{
    sk_cut(sk_pre);
    sk_put((const uchar *)mk_pre, strlen(mk_pre));
    sk_rstart = gs;
    sk_put(sk_rd, sk_rdn);
    sk_okpos = -1;
    sk_okc = 0;
    sk_okn = 0;
    sk_state = SK_PRE;
}

/* accept the candidate, learn it when it was not the first one */
static void sk_accept(void)
{
    const char *cd = sk_cand[sk_ci];
    int cn = strlen(cd);
    if (sk_ci > 0 && sk_state == SK_SEL) user_add(sk_key, sk_keyn, (const uchar *)cd, cn);
    sk_cut(sk_pre);
    sk_put((const uchar *)cd, cn);
    sk_put(sk_okana, sk_okn);
    sk_state = SK_IDLE;
    sk_okpos = -1;
    sk_okc = 0;
    sk_okn = 0;
    msgp = 0;
}

static void sk_register(void)
{
    char prompt[100], word[60];
    int n, i;
    strcpy(prompt, M[M_REG]);
    n = strlen(prompt);
    for (i = 0; i < sk_rdn && n < 90; i++) prompt[n++] = sk_rd[i];
    if (sk_okc && n < 90) prompt[n++] = sk_okc;
    prompt[n++] = ':';
    prompt[n++] = ' ';
    prompt[n] = 0;
    word[0] = 0;
    if (askline_s(prompt, word, 58) && word[0]) {
        user_add(sk_key, sk_keyn, (const uchar *)word, strlen(word));
        sk_nc = 0;
        sk_cblen = 0;
        addcand((const uchar *)word, strlen(word));
        sk_ci = 0;
        sk_state = SK_SEL;
        sk_show();
        sk_accept();
        msgp = M[M_REGDONE];
    } else {
        sk_back();
        sk_state = SK_PRE;
    }
}

/* Space in a reading: convert */
static void sk_convert(void)
{
    int end, i;
    sk_flush();
    if (sk_okpos >= 0) {
        sk_okn = gs - (sk_okpos + 1);
        if (sk_okn > 11) sk_okn = 11;
        for (i = 0; i < sk_okn; i++) sk_okana[i] = buf[sk_okpos + 1 + i];
        end = sk_okpos;
    } else {
        sk_okn = 0;
        end = gs;
    }
    sk_rdn = end - sk_rstart;
    if (sk_rdn <= 0 || sk_rdn > 60) {
        sk_commit_reading();
        return;
    }
    memcpy(sk_rd, buf + sk_rstart, sk_rdn);
    memcpy(sk_key, sk_rd, sk_rdn);
    sk_keyn = sk_rdn;
    if (sk_okpos >= 0) sk_key[sk_keyn++] = sk_okc;
    sk_lookup(sk_key, sk_keyn);
    if (sk_nc == 0) {
        sk_cut(sk_pre);
        sk_put((const uchar *)mk_sel, strlen(mk_sel));
        sk_put(sk_rd, sk_rdn);
        sk_state = SK_SEL;
        msgp = dict_ok ? M[M_NOCAND] : M[M_NODIC];
        sk_register();
        return;
    }
    sk_ci = 0;
    sk_state = SK_SEL;
    sk_show();
}

/* ------------------------------------------------------------------- key handling */

static int sk_islower(int c) { return c >= 'a' && c <= 'z'; }
static int sk_isupper(int c) { return c >= 'A' && c <= 'Z'; }

/* map the punctuation keys of kana mode: returns the kana index or -1 */
static int sk_punct(int c)
{
    switch (c) {
    case ',': return 0x61;
    case '.': return 0x62;
    case '-': return 0x60;
    case '[': return 0x63;
    case ']': return 0x64;
    }
    return -1;
}

/* A key was typed.  Returns 1 when the SKK input method used it up. */
static int skk_key(int c)
{
    int k;
    if (!enc) return 0;

    if (sk_state == SK_SEL) {
        if (c == ' ') {
            if (sk_ci + 1 >= sk_nc) {
                msgp = 0;
                sk_register();
                return 1;
            }
            sk_ci = (sk_ci >= 4) ? sk_lbase + 7 : sk_ci + 1;
            if (sk_ci >= sk_nc) { sk_ci = sk_nc - 1; msgp = 0; sk_register(); return 1; }
            sk_show();
            return 1;
        }
        if (c == 'x') {
            if (sk_ci == 0) { sk_back(); return 1; }
            sk_ci = (sk_ci > 4) ? (sk_lbase >= 11 ? sk_lbase - 7 : 3) : sk_ci - 1;
            if (sk_ci < 0) sk_ci = 0;
            if (sk_ci < 4) msgp = 0;
            sk_show();
            return 1;
        }
        if (c == 7) { msgp = 0; sk_back(); return 1; }
        if (sk_ci >= 4) {
            static const char lab[] = "asdfjkl";
            for (k = 0; k < 7; k++)
                if (c == lab[k] && sk_lbase + k < sk_nc) {
                    sk_ci = sk_lbase + k;
                    sk_accept();
                    return 1;
                }
        }
        sk_accept();
        if (c == '\r' || c == '\n') return 1;
        return skk_key(c);
    }

    if (sk_state == SK_PRE) {
        if (c == ' ') { sk_convert(); return 1; }
        if (c == '\r' || c == '\n') { sk_commit_reading(); return 1; }
        if (c == 7) { sk_cancel(); msgp = 0; return 1; }
        if (c == 8 || c == 0x7f) {
            if (sk_rn > 0) { sk_rn--; sk_setmsg(); return 1; }
            if (sk_okpos >= 0 && gs > sk_okpos + 1) sk_cut(prevc(gs));
            else if (sk_okpos >= 0) { sk_cut(sk_okpos); sk_okpos = -1; sk_okc = 0; }
            else if (gs > sk_rstart) sk_cut(prevc(gs));
            if (gs <= sk_rstart) sk_cancel();
            return 1;
        }
        if (sk_abbrev) {
            if (c >= 0x20 && c < 0x7f) { sk_putc(c); return 1; }
        } else {
            if (sk_isupper(c) && sk_rn == 0 && gs > sk_rstart && sk_okpos < 0) {
                sk_okpos = gs;
                sk_okc = c + 32;
                sk_putc('*');
                sk_romaji(c + 32);
                if (sk_trigger) { sk_trigger = 0; sk_convert(); }
                sk_setmsg();
                return 1;
            }
            if (sk_isupper(c)) c += 32;
            if (c == 'q' && sk_rn == 0) {
                if (gs > sk_rstart) { skk_mode = SK_KATA; sk_commit_reading(); skk_mode = SK_HIRA; }
                return 1;
            }
            if (sk_islower(c) || c == '\'') {
                sk_romaji(c);
                if (sk_trigger) { sk_trigger = 0; sk_convert(); }
                sk_setmsg();
                return 1;
            }
            k = sk_punct(c);
            if (k >= 0) {
                sk_flush();
                sk_emit(k, 255);
                return 1;
            }
        }
        sk_commit_reading();
        return skk_key(c);
    }

    /* idle */
    if (c == '\n') {
        if (skk_mode == SK_LATIN) { skk_mode = SK_HIRA; return 1; }
        return 0;
    }
    if (skk_mode == SK_LATIN) return 0;
    if (c == 7 && sk_rn) { sk_rn = 0; return 1; }
    if (c == '/' && sk_rn == 0) { sk_start(1); return 1; }
    if (sk_isupper(c)) {
        sk_flush();
        sk_start(0);
        c += 32;
        sk_romaji(c);
        sk_setmsg();
        return 1;
    }
    if (c == 'l' && sk_rn == 0) { skk_mode = SK_LATIN; return 1; }
    if (c == 'q' && sk_rn == 0) { skk_mode = (skk_mode == SK_HIRA) ? SK_KATA : SK_HIRA; return 1; }
    if (sk_islower(c) || (c == '\'' && sk_rn > 0)) {
        sk_romaji(c);
        sk_setmsg();
        return 1;
    }
    if (c < 0x100) {
        k = sk_punct(c);
        if (k >= 0) {
            sk_flush();
            sk_emit(k, 255);
            return 1;
        }
        if (sk_rn) sk_flush();
    }
    return 0;
}

static void skk_init(void)
{
    kpunct = enc == 2 ? kpunct_euc : enc == 3 ? kpunct_sjis : kpunct_utf8;
    mk_pre = enc == 2 ? mk_pre_euc : enc == 3 ? mk_pre_sjis : mk_pre_utf8;
    mk_sel = enc == 2 ? mk_sel_euc : enc == 3 ? mk_sel_sjis : mk_sel_utf8;
    rdD.fd = -1;
    if (enc && rd_open(&rdD, dict_name)) { dict_ok = 1; rd_close(&rdD); }
}
