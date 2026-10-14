/* SPDX-License-Identifier: MIT */
/* ipcbench - how long does a message round trip take?  (client -> FS server -> client) */
#include "types.h"
#include "mklib.h"

int main(void)
{
    u16 t0, t1, i;
    msg_t m;
    t0 = mk_ticks();
    while (mk_ticks() == t0)
        ;
    t0 = mk_ticks();
    for (i = 0; i < 2000; i++) {
        m.op = FS_SYNC;
        mk_call(TID_FS, &m);
    }
    t1 = mk_ticks();
    printf("2000 calls in %u ticks of 20 ms: %u microseconds per round trip (at 1.8432 MHz)\n", t1 - t0, (u16)(((unsigned long)(t1 - t0) * 20000UL) / 2000UL));
    return 0;
}
