/* SPDX-License-Identifier: MIT */
/* rm - delete files and empty directories */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    if (argc < 2) { puts("usage: rm file ..."); return 1; }
    for (i = 1; i < argc; i++) {
        r = unlink(argv[i]);
        if (r < 0) { printf("rm: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
