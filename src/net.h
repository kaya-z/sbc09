/* SPDX-License-Identifier: MIT */
/*
 * net.h - "OS-9 Net": network ports for the simulated machine
 *
 * ACIA1..ACIA3 can be attached to TCP sockets of the host:
 *   listen  : the port accepts a telnet/raw TCP connection and behaves like a
 *             terminal line (login on /T1../T3 with tsmon or "shell <>>>/t1")
 *   modem   : the port behaves like a Hayes modem; "ATDT host:port" opens an
 *             outgoing TCP connection, "ATA"/auto-answer accepts an incoming
 *             one; "+++" returns to command mode; "ATH" hangs up
 *   tcp     : the port is connected to host:port at start up
 *
 * See docs/OS9NET.md.
 */
#ifndef NET_H
#define NET_H

#include <poll.h>
#include "machine.h"

int      net_config(machine_t *m, const char *spec);   /* "-net" option */
void     net_poll(machine_t *m);
int      net_pollfds(machine_t *m, struct pollfd *fds, int max);
void     net_tx(machine_t *m, int idx, uint8_t c);
uint8_t  net_status_bits(machine_t *m, int idx);
void     net_shutdown(machine_t *m);
void     net_hup_ack(machine_t *m, int idx);

#endif
