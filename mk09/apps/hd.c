/* SPDX-License-Identifier: MIT */
/* hd - hexadecimal dump of a file */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int fd, n, i;
    u16 addr = 0;
    u8 buf[16];
    if (argc != 2) { puts("usage: hd file"); return 1; }
    fd = open(argv[1], FO_READ);
    if (fd < 0) { printf("hd: %s: %s\n", argv[1], strerr_(fd)); return 1; }
    while ((n = read(fd, buf, 16)) > 0) {
        printf("%04x ", addr);
        for (i = 0; i < 16; i++) {
            if (i < n) printf(" %02x", buf[i]);
            else putstr("   ");
        }
        putstr("  ");
        for (i = 0; i < n; i++) putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
        putchar('\n');
        addr += n;
    }
    close(fd);
    return 0;
}
