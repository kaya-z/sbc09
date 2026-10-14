/* SPDX-License-Identifier: MIT */
/* cat - copy files (or the standard input) to the standard output */
#include "types.h"
#include "mklib.h"

static char buf[256];

static int copy(int fd)
{
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        if (write(1, buf, n) < 0) return 1;
    return n < 0;
}

int main(int argc, char **argv)
{
    int i, fd, rc = 0;
    if (argc == 1) return copy(0);
    for (i = 1; i < argc; i++) {
        fd = open(argv[i], FO_READ);
        if (fd < 0) { printf("cat: %s: %s\n", argv[i], strerr_(fd)); rc = 1; continue; }
        rc |= copy(fd);
        close(fd);
    }
    return rc;
}
