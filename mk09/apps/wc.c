/* SPDX-License-Identifier: MIT */
/* wc - count lines, words and bytes of a file (or the standard input) */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int fd = 0, n, i;
    u8 buf[256];
    u16 lines = 0, words = 0, bhi = 0, blo = 0;
    u8 inword = 0;
    if (argc > 1) {
        fd = open(argv[1], FO_READ);
        if (fd < 0) { printf("wc: %s: %s\n", argv[1], strerr_(fd)); return 1; }
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t' || buf[i] == '\r') inword = 0;
            else if (!inword) { inword = 1; words++; }
        }
        if ((u16)(blo + n) < blo) bhi++;
        blo += n;
    }
    if (bhi) printf("%u %u (more than 64KB)\n", lines, words);
    else printf("%u %u %u\n", lines, words, blo);
    return 0;
}
