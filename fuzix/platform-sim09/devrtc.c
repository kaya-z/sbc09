/* SPDX-License-Identifier: GPL-2.0-only */
#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <rtc.h>

/*
 *	The clock of the simulator: writing $04 to the timer register ($E030)
 *	latches the time of the simulated machine into $E031-$E036 (year-1900,
 *	month 1-12, day, hour, minute, second).  Read only.
 */
#define TIMER_CMD	(*(volatile uint8_t *)0xE030)
#define RTC_REG(n)	(*(volatile uint8_t *)(0xE030 + (n)))

int plt_rtc_read(void)
{
	irqflags_t irq;
	uint16_t len = sizeof(struct cmos_rtc);
	uint16_t y;
	struct cmos_rtc cmos;
	uint8_t *p = cmos.data.bytes;

	if (udata.u_count < len)
		len = udata.u_count;

	irq = di();
	TIMER_CMD = 0x04;
	y = 1900 + RTC_REG(1);
	p[2] = RTC_REG(2) - 1;		/* the byte encoding counts months from 0 */
	p[3] = RTC_REG(3);
	p[4] = RTC_REG(4);
	p[5] = RTC_REG(5);
	p[6] = RTC_REG(6);
	irqrestore(irq);

	p[7] = 0;
	p[0] = y;
	p[1] = y >> 8;
	cmos.type = CMOS_RTC_DEC;
	if (uput(&cmos, udata.u_base, len) == -1)
		return -1;
	return len;
}

int plt_rtc_write(void)
{
	udata.u_error = EOPNOTSUPP;
	return -1;
}

int plt_rtc_ioctl(uarg_t request, char *data)
{
	udata.u_error = ENOTTY;
	return -1;
}
