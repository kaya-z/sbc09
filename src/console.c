/* SPDX-License-Identifier: MIT */
/*
 * console.c - terminal / script back end for ACIA0, character set conversion
 *
 * Credits: the raw terminal setup, the "escape character enters the
 * monitor" convention and the non blocking keyboard input are taken from
 * sbc09 src/io.c (L.C. Benschop, Shinji Kono, GPL v2).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>
#include <ctype.h>
#include <time.h>

#include "console.h"
#include "net.h"

volatile int console_escape;
int console_active;

static struct termios saved_term;
static int term_saved;
static int stdin_is_tty;
static int escchar = 0x1d;
static int stdin_flags;
static int kanji_mode = KANJI_RAW;
static FILE *infile;                       /* "S file" style type-in file */

/* ------------------------------------------------------------------ */
/* character set conversion                                            */
/* ------------------------------------------------------------------ */
static char host_cs[40] = "UTF-8";

static const char *kanji_name(int mode)
{
    return mode == KANJI_EUC ? "EUC-JP" : mode == KANJI_SJIS ? "CP932" :
           mode == KANJI_UTF8 ? "UTF-8" : NULL;
}

/* canonical charset family: "UTF-8", "EUC-JP" or "CP932" (Shift_JIS) */
static const char *cs_family(const char *n)
{
    char b[40];
    size_t i, k = 0;
    for (i = 0; n[i] && k < sizeof b - 1; i++)
        if (n[i] != '-' && n[i] != '_') b[k++] = (char)toupper((unsigned char)n[i]);
    b[k] = 0;
    if (!strcmp(b, "UTF8")) return "UTF-8";
    if (!strncmp(b, "EUCJP", 5) || !strcmp(b, "UJIS")) return "EUC-JP";
    if (!strcmp(b, "CP932") || !strcmp(b, "SJIS") || !strcmp(b, "SHIFTJIS") ||
        !strcmp(b, "WINDOWS31J") || !strcmp(b, "MS932") || !strcmp(b, "MSKANJI")) return "CP932";
    return n;
}

void console_set_hostcs(const char *name) { snprintf(host_cs, sizeof host_cs, "%s", name); }

void console_init_locale(void)
{
    const char *cs;
    setlocale(LC_CTYPE, "");
    cs = nl_langinfo(CODESET);
    if (!cs || !*cs || !strncmp(cs, "ANSI_X3.4", 9) || !strcmp(cs, "646")) cs = "UTF-8";
    console_set_hostcs(cs);
}

struct kconv {
    iconv_t cd;
    uint8_t pend[8];
    int npend;
};
typedef struct kconv kconv_t;

/* A streaming converter.  to_guest: host UTF-8 -> guest charset, otherwise
 * guest charset -> host UTF-8.  Returns NULL for the raw (no conversion) mode. */
kconv_t *kconv_open(int mode, int to_guest)
{
    const char *nm = kanji_name(mode);
    kconv_t *k;
    if (!nm) return NULL;
    if (!strcmp(cs_family(nm), cs_family(host_cs))) return NULL;   /* nothing to convert */
    k = calloc(1, sizeof *k);
    k->cd = to_guest ? iconv_open(nm, host_cs) : iconv_open(host_cs, nm);
    if (k->cd == (iconv_t)-1) {
        fprintf(stderr, "iconv: %s not available, using raw 8-bit mode\n", nm);
        free(k);
        return NULL;
    }
    return k;
}

void kconv_close(kconv_t *k)
{
    if (!k) return;
    iconv_close(k->cd);
    free(k);
}

/* convert a chunk, keeping an incomplete trailing sequence for next time */
size_t kconv_feed(kconv_t *c, const uint8_t *in, size_t n, uint8_t *out, size_t outsz)
{
    uint8_t buf[256];
    size_t total = 0;
    size_t i = 0;
    if (!c) {                                    /* raw: copy */
        if (n > outsz) n = outsz;
        memcpy(out, in, n);
        return n;
    }
    while (i < n) {
        size_t take = n - i;
        char *ip, *op;
        size_t il, ol;
        if (take > sizeof buf - (size_t)c->npend) take = sizeof buf - (size_t)c->npend;
        memcpy(buf, c->pend, (size_t)c->npend);
        memcpy(buf + c->npend, in + i, take);
        il = (size_t)c->npend + take;
        i += take;
        ip = (char *)buf;
        op = (char *)out + total;
        ol = outsz - total;
        c->npend = 0;
        while (il > 0) {
            size_t r = iconv(c->cd, &ip, &il, &op, &ol);
            if (r == (size_t)-1) {
                if (errno == EINVAL) {            /* incomplete sequence at the end */
                    memcpy(c->pend, ip, il);
                    c->npend = (int)il;
                    il = 0;
                } else if (errno == E2BIG) {
                    il = 0;
                } else {                          /* EILSEQ: replace one byte */
                    if (ol > 0) { *op++ = '?'; ol--; }
                    ip++; il--;
                }
            }
        }
        total = (size_t)((uint8_t *)op - out);
    }
    return total;
}

static kconv_t *host2guest, *guest2host;      /* console (ACIA0) converters */

static void conv_init(void)
{
    kconv_close(host2guest);
    kconv_close(guest2host);
    host2guest = kconv_open(kanji_mode, 1);
    guest2host = kconv_open(kanji_mode, 0);
    if (kanji_mode != KANJI_RAW && !host2guest) kanji_mode = KANJI_RAW;
}

/* one shot conversions of complete strings (file names) */
size_t kanji_host_to_guest(int mode, const char *in, size_t n, uint8_t *out, size_t outsz)
{
    kconv_t *k = kconv_open(mode, 1);
    size_t r = kconv_feed(k, (const uint8_t *)in, n, out, outsz);
    kconv_close(k);
    return r;
}

size_t kanji_guest_to_host(int mode, const uint8_t *in, size_t n, char *out, size_t outsz)
{
    kconv_t *k = kconv_open(mode, 0);
    size_t r = kconv_feed(k, in, n, (uint8_t *)out, outsz);
    kconv_close(k);
    return r;
}

void console_set_kanji(int mode) { kanji_mode = mode; conv_init(); }
int  console_kanji_mode(void) { return kanji_mode; }

/* ------------------------------------------------------------------ */
/* receive queue helpers                                               */
/* ------------------------------------------------------------------ */
int console_rx_full(acia_t *a)
{
    return ((a->rxhead + 1) % sizeof a->rxq) == a->rxtail;
}

void console_rx_push(acia_t *a, uint8_t c)
{
    if (console_rx_full(a)) return;
    a->rxq[a->rxhead] = c;
    a->rxhead = (a->rxhead + 1) % sizeof a->rxq;
}

/* ------------------------------------------------------------------ */
/* host terminal                                                       */
/* ------------------------------------------------------------------ */
static void on_sigint(int sig)
{
    (void)sig;
    console_escape = 1;
}

void console_set_escape(int ch) { escchar = ch; }

static void term_raw(void)
{
    struct termios t;
    if (!stdin_is_tty) return;
    tcgetattr(0, &saved_term);
    term_saved = 1;
    t = saved_term;
    t.c_iflag &= ~(INLCR | ICRNL | IXON | IXOFF);
    t.c_lflag &= ~(ECHO | ICANON);
    t.c_cc[VTIME] = 0;
    t.c_cc[VMIN] = 1;
    t.c_cc[VINTR] = (cc_t)escchar;
    t.c_cc[VQUIT] = _POSIX_VDISABLE;
    t.c_cc[VSUSP] = _POSIX_VDISABLE;
    tcsetattr(0, TCSAFLUSH, &t);
}

void console_setup(machine_t *m)
{
    acia_t *a = &m->acia[0];
    struct sigaction sa;
    if (a->kind) return;
    a->kind = ACIA_STDIO;
    stdin_is_tty = isatty(0);
    stdin_flags = fcntl(0, F_GETFL, 0);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    if (!console_script_active()) {
        term_raw();
        fcntl(0, F_SETFL, stdin_flags | O_NONBLOCK);
    }
    conv_init();
    console_active = 1;
}

static void flush_out(void);
void console_shutdown(void)
{
    flush_out();
    fflush(stdout);
    if (term_saved) tcsetattr(0, TCSAFLUSH, &saved_term);
    if (console_active) fcntl(0, F_SETFL, stdin_flags);
    term_saved = 0;
    console_active = 0;
    kconv_close(host2guest); kconv_close(guest2host);
    host2guest = guest2host = NULL;
}

/* temporarily give the terminal back (monitor prompt) */
void console_cooked_begin(void)
{
    fflush(stdout);
    if (term_saved) tcsetattr(0, TCSAFLUSH, &saved_term);
    fcntl(0, F_SETFL, stdin_flags);
}

void console_cooked_end(void)
{
    if (!console_script_active()) {
        term_raw();
        fcntl(0, F_SETFL, stdin_flags | O_NONBLOCK);
    }
}

int console_set_inputfile(const char *file)
{
    if (infile) fclose(infile);
    infile = fopen(file, "r");
    return infile ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* test scripts                                                        */
/* ------------------------------------------------------------------ */
#define MAXSTEPS 4096
enum { S_EXPECT, S_SEND, S_SLEEP, S_QUIT, S_TIMEOUT };
typedef struct { int op; char *text; long num; long num2; } sstep_t;
static sstep_t steps[MAXSTEPS];
static int nsteps, curstep, script_on, script_stat;
static long default_timeout_ms = 10000;
static uint64_t step_deadline;
static int step_started;
static char outring[16384];
static size_t outlen, outmark;
static uint8_t sendbuf[8192];
static size_t sendlen, sendpos;
static long send_gap = 3000;        /* cycles between bytes of a "send" */
static uint64_t next_send_cycle;

static char *unescape(const char *s, size_t *len)
{
    char *o = malloc(strlen(s) + 1);
    size_t n = 0;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'r': o[n++] = '\r'; break;
            case 'n': o[n++] = '\n'; break;
            case 't': o[n++] = '\t'; break;
            case 'e': o[n++] = 27; break;
            case '0': o[n++] = 0; break;
            case 'x': {
                int v = 0, k;
                for (k = 0; k < 2 && s[1] && strchr("0123456789abcdefABCDEF", s[1]); k++, s++)
                    v = v * 16 + (s[1] <= '9' ? s[1] - '0' : (s[1] | 32) - 'a' + 10);
                o[n++] = (char)v;
                break;
            }
            default: o[n++] = *s;
            }
        } else o[n++] = *s;
    }
    o[n] = 0;
    *len = n;
    return o;
}

/* script text is written in the host's UTF-8; convert to the guest charset */
static char *to_guest_text(const char *t)
{
    if (kanji_mode == KANJI_RAW) return strdup(t);
    {
        size_t n = strlen(t);
        uint8_t *o = malloc(n * 2 + 16);
        size_t k = kanji_host_to_guest(kanji_mode, t, n, o, n * 2 + 8);
        o[k] = 0;
        return (char *)o;
    }
}

static char *quoted(char *p, char **rest)
{
    char *q;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') { *rest = p; return NULL; }
    p++;
    q = p;
    while (*q && !(*q == '"' && q[-1] != '\\')) q++;
    if (*q) *q++ = 0;
    *rest = q;
    return p;
}

int console_load_script(const char *file)
{
    FILE *f = fopen(file, "r");
    char line[2048];
    if (!f) return -1;
    while (fgets(line, sizeof line, f) && nsteps < MAXSTEPS) {
        char *p = line, *rest;
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        if (!strncmp(p, "expect", 6)) {
            char *t = quoted(p + 6, &rest);
            if (!t) continue;
            steps[nsteps].op = S_EXPECT;
            steps[nsteps].text = to_guest_text(t);
            steps[nsteps].num = strtol(rest, NULL, 0);
            nsteps++;
        } else if (!strncmp(p, "send", 4)) {
            char *t = quoted(p + 4, &rest);
            size_t n;
            char *gt;
            if (!t) continue;
            gt = to_guest_text(t);
            steps[nsteps].op = S_SEND;
            steps[nsteps].text = unescape(gt, &n);
            free(gt);
            steps[nsteps].num = (long)n;
            nsteps++;
        } else if (!strncmp(p, "sleep", 5)) {
            steps[nsteps].op = S_SLEEP;
            steps[nsteps].num = strtol(p + 5, NULL, 0);
            nsteps++;
        } else if (!strncmp(p, "timeout", 7)) {
            steps[nsteps].op = S_TIMEOUT;
            steps[nsteps].num = strtol(p + 7, NULL, 0);
            nsteps++;
        } else if (!strncmp(p, "quit", 4)) {
            steps[nsteps].op = S_QUIT;
            steps[nsteps].num = strtol(p + 4, NULL, 0);
            nsteps++;
        } else {
            fprintf(stderr, "script: unknown command: %s\n", p);
        }
    }
    fclose(f);
    script_on = 1;
    script_stat = 0;
    return 0;
}

int console_script_active(void) { return script_on; }
int console_script_status(void) { return script_stat; }

static void script_out_char(uint8_t c)
{
    if (outlen == sizeof outring) {
        size_t half = sizeof outring / 2;
        memmove(outring, outring + half, outlen - half);
        outlen -= half;
        outmark = outmark > half ? outmark - half : 0;
    }
    outring[outlen++] = (char)c;
}

void console_script_step(machine_t *m)
{
    uint64_t now = m->cpu.total_cycles;
    if (!script_on || script_stat) return;
    /* feed pending send bytes */
    if (sendpos < sendlen) {
        acia_t *a = &m->acia[0];
        if (now >= next_send_cycle && !console_rx_full(a)) {
            console_rx_push(a, sendbuf[sendpos++]);
            next_send_cycle = now + (uint64_t)send_gap;
        }
        if (sendpos < sendlen) return;
    }
    while (curstep < nsteps) {
        sstep_t *s = &steps[curstep];
        long ms;
        switch (s->op) {
        case S_TIMEOUT:
            default_timeout_ms = s->num;
            curstep++;
            continue;
        case S_QUIT:
            script_stat = 1;
            m->exit_code = (int)s->num;
            m->stop = 1;
            return;
        case S_SLEEP:
            if (!step_started) { step_started = 1; step_deadline = now + (uint64_t)s->num * (uint64_t)m->clock_hz / 1000; }
            if (now < step_deadline) return;
            step_started = 0; curstep++;
            continue;
        case S_SEND:
            sendlen = (size_t)s->num;
            if (sendlen > sizeof sendbuf) sendlen = sizeof sendbuf;
            memcpy(sendbuf, s->text, sendlen);
            sendpos = 0;
            outmark = outlen;           /* expect only sees what follows */
            curstep++;
            return;
        case S_EXPECT: {
            size_t tl = strlen(s->text);
            char *hay = outring + outmark;
            size_t hl = outlen - outmark;
            ms = s->num ? s->num : default_timeout_ms;
            if (!step_started) { step_started = 1; step_deadline = now + (uint64_t)ms * (uint64_t)m->clock_hz / 1000; }
            if (hl >= tl) {
                char *f = memmem(hay, hl, s->text, tl);
                if (f) {
                    outmark = (size_t)(f - outring) + tl;
                    step_started = 0; curstep++;
                    continue;
                }
            }
            if (now > step_deadline) {
                fprintf(stderr, "\nscript: timeout waiting for \"%s\" (step %d)\n", s->text, curstep);
                script_stat = -1;
                m->exit_code = 2;
                m->stop = 1;
            }
            return;
        }
        }
    }
    script_stat = 1;
    m->stop = 1;
}

/* ------------------------------------------------------------------ */
/* poll / tx                                                           */
/* ------------------------------------------------------------------ */
static int stdin_read(machine_t *m, acia_t *a)
{
    uint8_t buf[256], out[1024];
    ssize_t n;
    size_t i, k;
    int got = 0;

    if (console_script_active()) return 0;
    if (getenv("MAXMON")) return 0;          /* debugging: stdin is for the monitor only */
    if (infile) {                            /* type-in file has priority */
        int c = fgetc(infile);
        if (c == EOF) { fclose(infile); infile = NULL; }
        else {
            if (c == '\n') c = '\r';
            console_rx_push(a, (uint8_t)c);
            return 1;
        }
    }
    n = read(0, buf, sizeof buf);
    if (n <= 0) return 0;
    for (i = 0; i < (size_t)n; i++)
        if (!stdin_is_tty && buf[i] == '\n') buf[i] = '\r';
    if (kanji_mode == KANJI_RAW) {
        for (i = 0; i < (size_t)n; i++) { console_rx_push(a, buf[i]); got++; }
    } else {
        k = kconv_feed(host2guest, buf, (size_t)n, out, sizeof out);
        for (i = 0; i < k; i++) { console_rx_push(a, out[i]); got++; }
    }
    (void)m;
    return got;
}

int console_poll(machine_t *m, int idx)
{
    acia_t *a = &m->acia[idx];
    if (a->kind == ACIA_STDIO) {
        static uint64_t last;
        if (a->rxhead != a->rxtail && m->cpu.total_cycles - last < 2000) return 0;
        last = m->cpu.total_cycles;
        if (console_rx_full(a)) return 0;
        if (idx == 0 && console_script_active()) { fflush(stdout); return 0; }
        return stdin_read(m, a);
    }
    return 0;
}

static uint8_t outpend[512];
static size_t outpendn;
static int outpend_idx = -1;

static void flush_out(void)
{
    if (outpendn) {
        char hb[2048];
        size_t n = kconv_feed(guest2host, outpend, outpendn, (uint8_t *)hb, sizeof hb);
        fwrite(hb, 1, n, stdout);
        outpendn = 0;
    }
    fflush(stdout);
}

void console_tx(machine_t *m, int idx, uint8_t c)
{
    acia_t *a = &m->acia[idx];
    if (a->kind == ACIA_NET) { net_tx(m, idx, c); return; }
    if (idx == 0) {
        if (console_script_active()) script_out_char(c);
        outpend[outpendn++] = c;
        outpend_idx = 0;
        /* flush on newline-ish boundaries or when the buffer fills; a
         * multibyte character is never split because conversion is stateful */
        if (outpendn >= sizeof outpend - 1 || c == '\n' || c == '\r' || outpendn > 64)
            flush_out();
        else {
            /* keep interactive output snappy: flush if nothing follows soon */
            static uint64_t lastflush;
            if (m->cpu.total_cycles - lastflush > 2000) { flush_out(); lastflush = m->cpu.total_cycles; }
        }
    }
    (void)outpend_idx;
}

void console_wait(machine_t *m, long timeout_us)
{
    struct pollfd fds[16];
    int n = 0;
    if (timeout_us < 0) timeout_us = 0;
    flush_out();
    if (console_active && !console_script_active() && m->acia[0].kind == ACIA_STDIO)
        { fds[n].fd = 0; fds[n].events = POLLIN; n++; }
    n += net_pollfds(m, fds + n, 16 - n);
    if (n == 0) {
        struct timespec ts = { timeout_us / 1000000, (timeout_us % 1000000) * 1000 };
        nanosleep(&ts, NULL);
    } else {
        poll(fds, n, (int)((timeout_us + 999) / 1000));
    }
}
