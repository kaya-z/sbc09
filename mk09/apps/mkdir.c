/* SPDX-License-Identifier: MIT */
/* mkdir - make directories */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    if (argc < 2) { puts("usage: mkdir dir ..."); return 1; }
    for (i = 1; i < argc; i++) {
        r = mkdir(argv[i]);
        if (r < 0) { printf("mkdir: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
