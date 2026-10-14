/* SPDX-License-Identifier: GPL-2.0-only */
#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <devtty.h>
#include <device.h>
#include <tty.h>
#include <timer.h>

/*
 *	The console is the ACIA of the simulator (a 6850 style device with
 *	status at $E000 and data at $E001). The simulator's ACIA raises no
 *	interrupts: the receiver is polled from the timer interrupt (50 times a
 *	second); the transmitter is always ready.
 */
#define ACIA_STATUS	(*(volatile uint8_t *)0xE000)
#define ACIA_DATA	(*(volatile uint8_t *)0xE001)
#define TIMER_REG	(*(volatile uint8_t *)0xE030)

static uint8_t tbuf1[TTYSIZ];

struct s_queue ttyinq[NUM_DEV_TTY + 1] = {
	/* ttyinq[0] is never used */
	{NULL, NULL, NULL, 0, 0, 0},
	{tbuf1, tbuf1, tbuf1, TTYSIZ, 0, TTYSIZ / 2},
};

tcflag_t termios_mask[NUM_DEV_TTY + 1] = {
	0,
	_CSYS,
};

/* Output for the system console (kprintf etc) */
void kputchar(uint_fast8_t c)
{
	if (c == '\n')
		tty_putc(1, '\r');
	tty_putc(1, c);
}

ttyready_t tty_writeready(uint_fast8_t minor)
{
	return TTY_READY_NOW;
}

void tty_putc(uint_fast8_t minor, uint_fast8_t c)
{
	ACIA_DATA = c;
}

void tty_sleeping(uint_fast8_t minor)
{
}

void tty_setup(uint_fast8_t minor, uint_fast8_t flags)
{
}

int tty_carrier(uint_fast8_t minor)
{
	return 1;
}

void tty_interrupt(void)
{
}

void tty_data_consumed(uint_fast8_t minor)
{
}

/* Called from the interrupt stub for every IRQ: the timer is the only source */
void plt_interrupt(void)
{
	while (ACIA_STATUS & 0x01)		/* receive data ready */
		tty_inproc(1, ACIA_DATA);

	if (TIMER_REG & 0x10) {			/* timer fired */
		TIMER_REG = 0x8F;		/* acknowledge and restart */
		timer_interrupt();
	}
}

void plt_reinterrupt(void)
{
	panic("reint");
}
