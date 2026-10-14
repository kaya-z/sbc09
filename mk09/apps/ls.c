/* SPDX-License-Identifier: MIT */
/* ls - list a directory: ls [-l] [-a] [path ...] */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

static u8 longfmt, all;

static void show(const dirent_t *d)
{
    if (!all && d->name[0] == '.') return;
    if (longfmt) {
        u16 dt = d->date, tm = d->time;
        char sz[12];
        if (d->attr & FA_DIR) strcpy(sz, "<DIR>");
        else {
            /* the size as a decimal number: 32 bits */
            unsigned long v = ((unsigned long)d->sizehi << 16) | d->sizelo;
            char t[12];
            u8 n = 0, k = 0;
            if (v == 0) t[n++] = '0';
            while (v) { t[n++] = (char)('0' + (u8)(v % 10)); v /= 10; }
            while (n) sz[k++] = t[--n];
            sz[k] = 0;
        }
        printf("%-13s %9s %04d-%02d-%02d %02d:%02d\n", d->name, sz, 1980 + (dt >> 9), (dt >> 5) & 15, dt & 31,
               tm >> 11, (tm >> 5) & 63);
    } else {
        printf("%s%s\n", d->name, (d->attr & FA_DIR) ? "/" : "");
    }
}

static int list(const char *p)
{
    dirent_t d;
    mkstat_t st;
    int fd, r;
    r = stat(p, &st);
    if (r < 0) { printf("ls: %s: %s\n", p, strerr_(r)); return 1; }
    if (!(st.attr & FA_DIR)) {
        char *s = strrchr(p, '/');
        memset(&d, 0, sizeof(d));
        strcpy(d.name, s ? s + 1 : p);
        d.attr = st.attr;
        d.sizehi = st.sizehi;
        d.sizelo = st.sizelo;
        show(&d);
        return 0;
    }
    fd = open(p, FO_DIR | FO_READ);
    if (fd < 0) { printf("ls: %s: %s\n", p, strerr_(fd)); return 1; }
    while (readdir(fd, &d) > 0) show(&d);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    int i, n = 0, rc = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            char *f = argv[i] + 1;
            while (*f) { if (*f == 'l') longfmt = 1; else if (*f == 'a') all = 1; f++; }
        } else {
            n++;
        }
    }
    if (n == 0) return list(".");
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        if (n > 1) printf("%s:\n", argv[i]);
        rc |= list(argv[i]);
    }
    return rc;
}
