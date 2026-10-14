/* SPDX-License-Identifier: MIT */
/* spin - loop until it is stopped with ^C (a test of the foreground program handling of PM) */
#include "types.h"
#include "mklib.h"

int main(void)
{
    u16 n = 0;
    puts("spinning (^C stops)");
    for (;;)
        n++;
    return n;
}
