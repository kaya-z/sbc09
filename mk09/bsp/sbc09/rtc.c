/* SPDX-License-Identifier: MIT */
/* rtc.c - sbc09 board: the real time clock */
#include "bsp.h"

void bsp_rtc_read(u8 *tm)
{
    u8 i;
    TIMER_REG = 0x04;               /* latch the clock */
    for (i = 0; i < 6; i++)
        tm[i] = RTC_REG(1 + i);
}

/* the time in FAT format: date = (year-1980) << 9 | month << 5 | day, time = hour << 11 | minute << 5 | second / 2 */
void bsp_now_fat(u16 *date, u16 *time)
{
    u8 tm[6];
    bsp_rtc_read(tm);
    if (tm[0] < 80) {                       /* no clock: 1980-01-01 */
        *date = (1 << 5) | 1;
        *time = 0;
        return;
    }
    *date = ((u16)(tm[0] - 80) << 9) | ((u16)tm[1] << 5) | tm[2];
    *time = ((u16)tm[3] << 11) | ((u16)tm[4] << 5) | (tm[5] >> 1);
}
