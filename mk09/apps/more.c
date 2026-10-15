/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * more - show a file one screen at a time
 *
 *   more file...        Space: next screen   Enter: next line   q: quit
 *
 * The screen size and the character set are those of the console (stty).  Long lines are wrapped
 * by the display width of the characters (a Japanese character is two columns wide, a half-width
 * kana one), so that the screen is never scrolled by the terminal behind the program's back.
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define OBUF 128

static u8 cs, cols, rows;
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

static void puts_(const char *s)
{
    while (*s) put((u8)*s++);
}

/* length in bytes and width in columns of the character that starts with c */
static u8 clen(u8 c)
{
    if (cs == CS_UTF8) return c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (cs == CS_EUC) return (c >= 0xA1 && c <= 0xFE) || c == 0x8E ? 2 : c == 0x8F ? 3 : 1;
    if (cs == CS_SJIS) return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC) ? 2 : 1;
    return 1;
}

static u8 cwidth(const u8 *p, u8 n)
{
    if (n == 1) return 1;
    if (cs == CS_UTF8) {
        if (p[0] == 0xEF && p[1] >= 0xBD && p[1] <= 0xBE) return 1;    /* U+FF61-FF9F */
        return 2;
    }
    if (cs == CS_EUC) return p[0] == 0x8E ? 1 : 2;
    return 2;
}

static u8 col, row, pg, maxc, pending, quit;

static void ask(void)
{
    int k;
    flush();
    puts_("-- more --");
    flush();
    for (;;) {
        k = getchar();
        if (k == 'q' || k == 'Q' || k == 3 || k < 0) { quit = 1; break; }
        if (k == ' ') { row = 0; break; }
        if (k == '\r' || k == '\n') { row = pg - 1; break; }
    }
    puts_("\r          \r");
    flush();
    pending = 0;
}

static void nl(void)
{
    if (pending) ask();
    if (quit) return;
    put('\n');                       /* the console turns it into CR LF */
    col = 0;
    if (++row >= pg) pending = 1;      /* ask when something more comes */
}

static void more_file(int fd)
{
    u8 buf[128], ch[4];
    u8 w, n, have = 0, need = 0, c, t;
    int r, i, k;

    while (!quit && (r = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < r && !quit; i++) {
            c = buf[i];
            if (!have && c == '\r') continue;
            if (!have && c == '\n') { nl(); continue; }
            if (!have && c == '\t') {
                t = (u8)(8 - (col & 7));
                if (col + t > maxc) nl();
                if (quit) break;
                if (pending) ask();
                while (t--) { put(' '); col++; }
                continue;
            }
            if (!have) need = clen(c);
            ch[have++] = c;
            if (have < need) continue;
            n = have;
            have = 0;
            if (n == 1 && c < 0x20) continue;
            w = cwidth(ch, n);
            if (col + w > maxc) nl();
            if (quit) break;
            if (pending) ask();
            for (k = 0; k < n; k++) put(ch[k]);
            col += w;
        }
    }
    flush();
}

int main(int argc, char **argv)
{
    u8 mode;
    int i, fd;
    if (argc < 2) { puts("usage: more file..."); return 1; }
    tty_getmode(&mode, &cs, &cols, &rows);
    if (!cols) cols = 80;
    if (!rows) rows = 24;
    pg = rows > 2 ? rows - 1 : 1;                /* lines per screen */
    maxc = cols > 8 ? cols - 1 : cols;           /* the last column stays free (no autowrap surprises) */
    tty_setmode((mode | TM_RAW) & ~TM_ECHO, 0xFF, 0xFF, 0xFF);
    for (i = 1; i < argc && !quit; i++) {
        fd = open(argv[i], FO_READ);
        if (fd < 0) { printf("more: %s: %s\n", argv[i], strerr_(fd)); continue; }
        col = row = pending = 0;
        more_file(fd);
        close(fd);
    }
    flush();
    tty_setmode(mode, 0xFF, 0xFF, 0xFF);
    return 0;
}
