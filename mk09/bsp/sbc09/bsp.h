/* SPDX-License-Identifier: MIT */
/*
 * bsp.h - board support: the sbc09 machine (sim09, and the Tang Nano 9K implementation)
 *
 *   $E000 ACIA control/status   $E001 ACIA data        (6850 style, polled)
 *   $E00F power control         ($A5 = power off)
 *   $E030 timer                 ($8F = start/acknowledge, bit 4 of the status = fired)
 *   $E031-$E036                 RTC: year-1900 month day hour min sec (after writing 4 to $E030)
 *   $E040 disk command/result   $E041 drive, $E042-$E044 sector (256 byte), $E045-$E046 DMA address
 *
 * The disk is accessed in 256 byte sectors by DMA ($81 = read, $55 = write); the card is not
 * seen by the CPU in any other way.
 */
#ifndef BSP_H
#define BSP_H

#include "types.h"

#define IO_BASE         0xE000u
#define ACIA_STATUS     (*(u8 *)0xE000)
#define ACIA_DATA       (*(u8 *)0xE001)
#define ACIA_RDRF       0x01
#define ACIA_TDRE       0x02
#define POWER_REG       (*(u8 *)0xE00F)
#define TIMER_REG       (*(u8 *)0xE030)
#define RTC_REG(n)      (*(u8 *)(0xE030 + (n)))
#define DISK_CMD        (*(u8 *)0xE040)
#define DISK_DRIVE      (*(u8 *)0xE041)
#define DISK_LSN_HI     (*(u8 *)0xE042)
#define DISK_LSN_MID    (*(u8 *)0xE043)
#define DISK_LSN_LO     (*(u8 *)0xE044)
#define DISK_DMA        (*(u16 *)0xE045)

#define DISK_READ_CMD   0x81
#define DISK_WRITE_CMD  0x55

/* kernel services of the board */
void bsp_timer_start(void);      /* start the 50 Hz tick */
u8   bsp_timer_fired(void);      /* 1 if the tick fired (and acknowledge it) */
void bsp_power_off(void);

/* for the servers */
void bsp_rtc_read(u8 *tm);       /* tm[0..5] = year-1900, month, day, hour, min, sec */
void bsp_now_fat(u16 *date, u16 *time);   /* the clock in the FAT format */

#endif
