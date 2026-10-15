/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * grep - print the lines that match a pattern
 *
 *   grep [-i] [-n] [-v] [-c] pattern [file...]
 *
 * The pattern is a small regular expression: a character, . (any one character), * (the character
 * before it, any number of times), ^ and $ (the start and the end of the line), \ (the next character
 * is taken as it is).  The characters of the console's character set (UTF-8, EUC-JP, Shift_JIS) are
 * units: "." matches a whole Japanese character, and a second byte of Shift_JIS is never taken for
 * a special character.  -i ignores the case of ASCII letters.  Without a file the standard input
 * is read (grep x < file).  A line is at most 255 bytes (a longer line is cut in pieces).
 * Exit code: 0 if a line matched, 1 if not, 2 on an error.
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define LINEMAX 256
#define OBUF 128

static u8 cs, fi;
static const u8 *pat;       /* the pattern */

static u8 clen(u8 c)
{
    if (cs == CS_UTF8) return c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (cs == CS_EUC) return (c >= 0xA1 && c <= 0xFE) || c == 0x8E ? 2 : c == 0x8F ? 3 : 1;
    if (cs == CS_SJIS) return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC) ? 2 : 1;
    return 1;
}

static u8 lower(u8 c)
{
    return (fi && c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c;
}

/* does the character at t (n bytes) equal the one at p? */
static int same(const u8 *p, const u8 *t, u8 n)
{
    u8 i;
    for (i = 0; i < n; i++)
        if (lower(p[i]) != lower(t[i])) return 0;
    return 1;
}

static int matchhere(const u8 *p, const u8 *t);

/* el (n bytes, or any character when any is set) repeated any number of times, then rest */
static int matchstar(const u8 *el, u8 n, u8 any, const u8 *rest, const u8 *t)
{
    u8 m;
    for (;;) {
        if (matchhere(rest, t)) return 1;
        if (!*t) return 0;
        m = clen(*t);
        if (!any && (m != n || !same(el, t, n))) return 0;
        t += m;
    }
}

static int matchhere(const u8 *p, const u8 *t)
{
    const u8 *el;
    u8 n, m, any;
    for (;;) {
        if (!*p) return 1;
        if (p[0] == '$' && !p[1]) return *t == 0;
        el = p;
        any = 0;
        if (*p == '\\' && p[1]) { el = p + 1; n = 1; p += 2; }
        else if (*p == '.') { any = 1; n = 1; p++; }
        else { n = clen(*p); p += n; }
        if (*p == '*') return matchstar(el, n, any, p + 1, t);
        if (!*t) return 0;
        m = clen(*t);
        if (!any && (m != n || !same(el, t, n))) return 0;
        t += m;
    }
}

static int match(const u8 *t)
{
    const u8 *p = pat;
    if (*p == '^') return matchhere(p + 1, t);
    for (;;) {
        if (matchhere(p, t)) return 1;
        if (!*t) return 0;
        t += clen(*t);
    }
}

static u8 ob[OBUF];
static u8 on;

static void flush(void)
{
    if (on) write(1, ob, on);
    on = 0;
}

static void put(u8 c)
{
    if (on == OBUF) flush();
    ob[on++] = c;
}

static void putnum(u16 v)
{
    char s[6];
    utoa_(v, s, 10);
    {
        char *q = s;
        while (*q) put((u8)*q++);
    }
}

static u8 opt_n, opt_v, opt_c;
static u16 found;       /* lines that matched in this file */
static u16 total;

static void line(const char *name, u16 no, u8 *l, u16 len)
{
    u8 *q;
    l[len] = 0;
    if (len && l[len - 1] == '\r') l[--len] = 0;
    if (match(l) == opt_v) return;     /* match 0/1 against opt_v 0/1: print when they differ */
    found++;
    if (opt_c) return;
    if (name) { for (q = (u8 *)name; *q; q++) put(*q); put(':'); }
    if (opt_n) { putnum(no); put(':'); }
    for (q = l; *q; q++) put(*q);
    put('\n');
}

static int grep_file(int fd, const char *name)
{
    static u8 buf[128];
    static u8 l[LINEMAX + 1];
    u16 len = 0, no = 1;
    int r, i;
    found = 0;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < r; i++) {
            if (buf[i] == '\n') { line(name, no++, l, len); len = 0; }
            else if (len < LINEMAX) l[len++] = buf[i];
            else { line(name, no, l, len); len = 0; l[len++] = buf[i]; }
        }
    }
    if (len) line(name, no, l, len);
    if (opt_c) {
        const char *q = name;
        if (q) { while (*q) put((u8)*q++); put(':'); }
        putnum(found);
        put('\n');
    }
    total += found;
    return 0;
}

int main(int argc, char **argv)
{
    u8 mode, cols, rows;
    int i = 1, fd, rc = 0;
    char *a;
    while (i < argc && argv[i][0] == '-' && argv[i][1]) {
        for (a = argv[i] + 1; *a; a++) {
            if (*a == 'i') fi = 1;
            else if (*a == 'n') opt_n = 1;
            else if (*a == 'v') opt_v = 1;
            else if (*a == 'c') opt_c = 1;
            else { printf("grep: -%c?\n", *a); return 2; }
        }
        i++;
    }
    if (i >= argc) { puts("usage: grep [-i] [-n] [-v] [-c] pattern [file...]"); return 2; }
    tty_getmode(&mode, &cs, &cols, &rows);
    pat = (const u8 *)argv[i++];
    if (i >= argc) {
        grep_file(0, 0);
    } else {
        u8 many = (u8)(argc - i > 1);
        for (; i < argc; i++) {
            fd = open(argv[i], FO_READ);
            if (fd < 0) { flush(); printf("grep: %s: %s\n", argv[i], strerr_(fd)); rc = 2; continue; }
            if (many) grep_file(fd, argv[i]); else grep_file(fd, 0);
            close(fd);
        }
    }
    flush();
    if (rc) return rc;
    return total ? 0 : 1;
}
