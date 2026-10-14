/* SPDX-License-Identifier: MIT */
/* mv - rename a file or directory: mv from to  (to may be a directory) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

static char dst[MAXPATH + 14];

int main(int argc, char **argv)
{
    mkstat_t st;
    const char *to;
    char *base;
    int r;

    if (argc != 3) { puts("usage: mv from to"); return 1; }
    to = argv[2];
    if (stat(to, &st) == 0 && (st.attr & FA_DIR)) {
        base = strrchr(argv[1], '/');
        base = base ? base + 1 : argv[1];
        if (strlen(to) + strlen(base) + 2 > sizeof(dst)) { puts("mv: name too long"); return 1; }
        strcpy(dst, to);
        if (dst[strlen(dst) - 1] != '/') strcat(dst, "/");
        strcat(dst, base);
        to = dst;
    }
    r = rename(argv[1], to);
    if (r < 0) { printf("mv: %s\n", strerr_(r)); return 1; }
    return 0;
}
