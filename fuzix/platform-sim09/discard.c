/* SPDX-License-Identifier: GPL-2.0-only */
#include <kernel.h>
#include <timer.h>
#include <kdata.h>
#include <printf.h>
#include <device.h>
#include <devtty.h>

void map_init(void)
{
	kprintf("sim09: MC6809, 64K RAM, 29K for processes\n");
}
