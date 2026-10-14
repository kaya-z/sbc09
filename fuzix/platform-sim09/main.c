/* SPDX-License-Identifier: GPL-2.0-only */
#include <kernel.h>
#include <timer.h>
#include <kdata.h>
#include <printf.h>
#include <device.h>
#include <devtty.h>

uint16_t swap_dev = 0xFFFF;

uint8_t plt_param(char *p)
{
    return 0;
}

void do_beep(void)
{
}

void plt_discard(void)
{
}
