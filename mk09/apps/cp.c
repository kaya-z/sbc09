/* SPDX-License-Identifier: MIT */
/* cp - copy a file: cp from to  (to may be a directory) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

static char buf[512];
static char dst[MAXPATH + 14];

int main(int argc, char **argv)
{
    int in, out, n, w;
    mkstat_t st;
    const char *to;
    char *base;

    if (argc != 3) { puts("usage: cp from to"); return 1; }
    to = argv[2];
    if (stat(to, &st) == 0 && (st.attr & FA_DIR)) {         /* into a directory */
        base = strrchr(argv[1], '/');
        base = base ? base + 1 : argv[1];
        if (strlen(to) + strlen(base) + 2 > sizeof(dst)) { puts("cp: name too long"); return 1; }
        strcpy(dst, to);
        if (dst[strlen(dst) - 1] != '/') strcat(dst, "/");
        strcat(dst, base);
        to = dst;
    }
    in = open(argv[1], FO_READ);
    if (in < 0) { printf("cp: %s: %s\n", argv[1], strerr_(in)); return 1; }
    out = open(to, FO_WRITE | FO_CREATE | FO_TRUNC);
    if (out < 0) { printf("cp: %s: %s\n", to, strerr_(out)); close(in); return 1; }
    for (;;) {
        n = read(in, buf, sizeof(buf));
        if (n <= 0) break;
        w = write(out, buf, n);
        if (w != n) { printf("cp: %s: %s\n", to, strerr_(w < 0 ? w : -E_NOSPC)); n = -1; break; }
    }
    close(in);
    close(out);
    return n < 0;
}
