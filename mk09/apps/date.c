/* SPDX-License-Identifier: MIT */
/* date - show the date and time of the clock */
#include "types.h"
#include "mklib.h"

int main(void)
{
    u16 d, t;
    if (gettime(&d, &t) < 0) { puts("date: no clock"); return 1; }
    printf("%04d-%02d-%02d %02d:%02d:%02d\n", 1980 + (d >> 9), (d >> 5) & 15, d & 31, t >> 11, (t >> 5) & 63, (t & 31) * 2);
    return 0;
}
