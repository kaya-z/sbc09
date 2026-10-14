/* SPDX-License-Identifier: MIT */
/*
 * dbg.h - instruction trace, breakpoints and the interactive "v09>" style monitor
 */
#ifndef DBG_H
#define DBG_H

#include "machine.h"

int  dbg_before_insn(machine_t *m);   /* non-zero: stop the run loop         */
void dbg_escape(machine_t *m);        /* escape key pressed: enter monitor   */
void dbg_monitor(machine_t *m);
void dbg_trace(machine_t *m, FILE *fp);
int  dbg_set_break(machine_t *m, uint16_t addr);
const char *os9_svc_name(int code);
extern int dbg_os9trace;              /* -os9trace */

#endif
