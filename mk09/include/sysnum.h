/* SPDX-License-Identifier: MIT */
/* sysnum.h - system call numbers (also turned into an assembler include by the Makefile) */
#ifndef SYSNUM_H
#define SYSNUM_H
#define SYS_SEND        1
#define SYS_RECV        2
#define SYS_CALL        3
#define SYS_CALLG       4
#define SYS_REPLY       5
#define SYS_NOTIFY      6
#define SYS_COPYFROM    7
#define SYS_COPYTO      8
#define SYS_SLEEP       9
#define SYS_TICKS       10
#define SYS_YIELD       11
#define SYS_EXIT        12
#define SYS_SELF        13
#define SYS_TINFO       14
#define SYS_HOOK        16
#define SYS_TCREATE     17
#define SYS_TSTART      18
#define SYS_TKILL       19
#define SYS_TLOAD       20
#define SYS_TREAP       21
#define SYS_POWER       22
#endif
