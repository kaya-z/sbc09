/* SPDX-License-Identifier: GPL-2.0-only */
#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <tinydisk.h>
#include <device.h>

/*
 *	The raw disks of the simulator (-0 and -1 image files).  A DMA style
 *	command port at $E040: drive ($E041), 24 bit sector number ($E042-44,
 *	256 byte sectors), memory address ($E045-46), command ($E040: $81 read,
 *	$55 write); the result is left in $E040 (0 = good).  A Fuzix block is two
 *	sectors.
 */
#define DISK_CMD	((volatile uint8_t *)0xE040)
#define DISK_DRIVE	((volatile uint8_t *)0xE041)
#define DISK_LSN	((volatile uint8_t *)0xE042)
#define DISK_ADDR	((volatile uint8_t *)0xE045)

/* gcc6809 turns "store; read back; compare" into a test of the stored value, so the
   status is read in a function of its own */
static uint8_t __attribute__((noinline)) disk_status(void)
{
	return DISK_CMD[0];
}

static int sim_sector(uint8_t unit, bool is_read, uint32_t lsn, uint8_t *dptr)
{
	DISK_DRIVE[0] = unit;
	DISK_LSN[0] = (uint8_t)(lsn >> 16);
	DISK_LSN[1] = (uint8_t)(lsn >> 8);
	DISK_LSN[2] = (uint8_t)lsn;
	DISK_ADDR[0] = (uint8_t)((uint16_t)dptr >> 8);
	DISK_ADDR[1] = (uint8_t)(uint16_t)dptr;
	DISK_CMD[0] = is_read ? 0x81 : 0x55;
	return disk_status() == 0;
}

static int sim_xfer(uint_fast8_t unit, bool is_read, uint32_t block, uint8_t *dptr)
{
	uint32_t lsn = block << 1;
	if (!sim_sector(unit, is_read, lsn, dptr))
		return 0;
	return sim_sector(unit, is_read, lsn + 1, dptr + 256);
}

__attribute__((section(".discard")))
void devhd_init(void)
{
	/* Both disks, with a partition table; a drive without an image is skipped quietly.
	   Page 2 (0200-02FF) is free at this point: it is the scratch sector of the probe. */
	uint8_t unit;
	for (unit = 0; unit < 2; unit++)
		if (sim_sector(unit, true, 0, (uint8_t *)0x0200))
			td_register(unit, sim_xfer, td_ioctl_none, 1);
}
