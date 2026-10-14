/* SPDX-License-Identifier: MIT */
/* bsp.c - sbc09 board: timer, power control, RTC */
#include "bsp.h"

void bsp_timer_start(void)
{
    TIMER_REG = 0x8F;               /* start; the first tick comes one period later */
}

/* returns 1 when the timer has fired; acknowledges it (and restarts it) */
u8 bsp_timer_fired(void)
{
    if (TIMER_REG & 0x10) {
        TIMER_REG = 0x8F;
        return 1;
    }
    return 0;
}

void bsp_power_off(void)
{
    POWER_REG = 0xA5;
    for (;;)
        ;
}
