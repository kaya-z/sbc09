/* SPDX-License-Identifier: MIT */
/* ps - the tasks of the system */
#include "types.h"
#include "mklib.h"

static const char st_names[] = "free   ready  receivesend   sleep  zombie new    running";

int main(void)
{
    taskinfo_t ti;
    u8 t, k;
    puts("TID STATE   PRIO  REGION         SP    CPU");
    for (t = 0; t < NTASK; t++) {
        if (mk_tinfo(t, &ti) || ti.state == TS_FREE) continue;
        printf("%3d ", t);
        for (k = 0; k < 7; k++) putchar(st_names[ti.state * 7 + k]);
        printf(" %d    %04x-%04x  %04x  %u\n", ti.prio, ti.lo, ti.hi, ti.sp, ti.cpu);
    }
    return 0;
}
