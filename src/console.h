/* SPDX-License-Identifier: MIT */
/*
 * console.h - host side of the serial ports (ACIA emulation back ends)
 *
 *   ACIA0 is normally the host terminal (stdin/stdout, raw mode) or a test
 *   script.  ACIA1..3 are network ports (see net.h).
 *
 * Character set handling for Japanese text: the guest may use UTF-8
 * (pass-through, the default), EUC-JP or Shift_JIS (CP932).  Conversion to
 * and from the host terminal's UTF-8 is done with iconv(3).
 *
 * Credits: raw terminal handling, escape-to-monitor key and the xmodem-free
 * "ACIA at $E000" model follow sbc09 src/io.c (GPL v2).
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "machine.h"

enum { ACIA_NONE = 0, ACIA_STDIO = 1, ACIA_NET = 2 };

/* guest character set; KANJI_RAW = same as the host terminal, no conversion */
enum { KANJI_RAW = 0, KANJI_EUC = 1, KANJI_SJIS = 2, KANJI_UTF8 = 3 };

extern volatile int console_escape;     /* set by SIGINT (escape char)   */
extern int          console_active;

void console_setup(machine_t *m);        /* attach ACIA0 to the terminal   */
void console_shutdown(void);
void console_set_escape(int ch);
void console_cooked_begin(void);          /* give the tty back (monitor)    */
void console_cooked_end(void);
void console_set_kanji(int mode);
void console_set_hostcs(const char *name);   /* host terminal charset (default: from the locale) */
void console_init_locale(void);
int  console_kanji_mode(void);
int  console_poll(machine_t *m, int idx);            /* returns bytes queued */
void console_tx(machine_t *m, int idx, uint8_t c);
void console_wait(machine_t *m, long timeout_us);    /* sleep, wake on input */
void console_rx_push(acia_t *a, uint8_t c);
int  console_rx_full(acia_t *a);

/* test scripts: expect/send/sleep/quit, see docs/ */
int  console_load_script(const char *file);
int  console_script_active(void);
int  console_script_status(void);        /* 0 running, 1 finished ok, <0 failed */
void console_script_step(machine_t *m);

/* input file (typed in before the keyboard is read, like the monitor's "S") */
int  console_set_inputfile(const char *file);

/* charset conversion helpers (also used by vdisk for file names) */
size_t kanji_guest_to_host(int mode, const uint8_t *in, size_t n, char *out, size_t outsz);
size_t kanji_host_to_guest(int mode, const char *in, size_t n, uint8_t *out, size_t outsz);

#endif
