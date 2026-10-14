/* SPDX-License-Identifier: MIT */
/*
 * stk - how deep did the stacks of the system tasks go?  (a diagnostic tool)
 *
 * The servers fill their unused stack with $A5 when they start (lib/crt0.s).  This program looks
 * at the memory of the tasks (the machine has no memory protection) and reports, for each task,
 * the size of its region and how many bytes of its stack area have never been touched.
 */
#include "types.h"
#include "mklib.h"

int main(void)
{
    taskinfo_t ti;
    u8 t;
    u16 a, run, best, bestend;
    const u8 *p;

    puts("TID REGION         UNUSED STACK (bytes)");
    for (t = 1; t < NTASK; t++) {
        if (mk_tinfo(t, &ti) || ti.state == TS_FREE) continue;
        p = (const u8 *)ti.lo;
        run = best = bestend = 0;
        for (a = 0; a < ti.hi - ti.lo; a++) {
            if (p[a] == 0xA5) {
                run++;
                if (run > best) { best = run; bestend = a + 1; }
            } else {
                run = 0;
            }
        }
        printf("%3d %04x-%04x  %5u (the run ends %u bytes below the top)\n", t, ti.lo, ti.hi, best, ti.hi - ti.lo - bestend);
    }
    return 0;
}
