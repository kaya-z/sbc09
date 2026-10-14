/* SPDX-License-Identifier: MIT */
/* df - space of the file system */
#include "types.h"
#include "mklib.h"

int main(void)
{
    u8 spc;
    u16 total, freec;
    int r = statfs(&spc, &total, &freec);
    if (r < 0) { printf("df: %s\n", strerr_(r)); return 1; }
    printf("cluster %d bytes, %u clusters, %u free (%u KB free of %u KB)\n", spc * 512, total, freec,
           (u16)(((unsigned long)freec * spc) / 2), (u16)(((unsigned long)total * spc) / 2));
    return 0;
}
