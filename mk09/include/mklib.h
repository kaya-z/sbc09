/* SPDX-License-Identifier: MIT */
/*
 * mklib.h - the C library of MK09 programs (lib/libmk.c)
 *
 * File descriptors: 0 = standard input, 1 = standard output, 2 = standard error (the console,
 * or a file when the shell redirected it), 3 and up = files and directories of the FS server.
 * Functions return a value >= 0, or the negated error code (E_* of mk09.h).
 */
#ifndef MKLIB_H
#define MKLIB_H

#include "types.h"
#include "mk09.h"
#include "proto.h"

/* the start-up block that PM puts at the top of a program's memory:
 *   [0] version  [1] stdin handle  [2] stdout handle  [3] argc  [4..] argument strings */
#define SB_VERSION  1
#define SB_IN       1
#define SB_OUT      2
#define SB_ARGC     3
#define SB_ARGS     4

/* files */
int  open(const char *path, u8 flags);
int  close(int fd);
int  read(int fd, void *buf, u16 n);
int  write(int fd, const void *buf, u16 n);
int  seek(int fd, u16 hi, u16 lo, u8 whence, u16 *rhi, u16 *rlo);   /* (hi, lo) signed */
int  readdir(int fd, dirent_t *d);                  /* open a directory with open(path, FO_DIR | FO_READ) */
int  mkdir(const char *path);
int  unlink(const char *path);
int  rename(const char *from, const char *to);
int  chdir(const char *path);
int  getcwd(char *buf, u16 n);

typedef struct mkstat {
    u8  attr;
    u16 sizehi, sizelo;
    u16 date;
} mkstat_t;
int  stat(const char *path, mkstat_t *st);
int  fstat(int fd, mkstat_t *st);
int  statfs(u8 *spc, u16 *clusters, u16 *freec);
int  sync(void);

/* console */
int  tty_getmode(u8 *mode, u8 *charset, u8 *cols, u8 *rows);
int  tty_setmode(u8 mode, u8 charset, u8 cols, u8 rows);   /* 0xFF: keep the charset / the size */
int  tty_ready(void);
int  tty_flush(void);

/* processes */
int  spawn(const char *block, u16 len, u16 in_h, u16 out_h);  /* "prog\0arg\0...\0\0"; returns the exit code */
int  gettime(u16 *date, u16 *time);                           /* FAT format */
void shutdown(void);

/* stdio (buffered output on file descriptor 1) */
int  putchar(int c);
int  puts(const char *s);
int  putstr(const char *s);                         /* without the newline */
int  printf(const char *fmt, ...);
int  getchar(void);
int  fflush(void);                                  /* flush stdout */
int  gets_(char *buf, u16 n);                       /* read a line (without the newline); -1 at EOF */
void exit(int code);

/* strings */
const char *strerr_(int e);                        /* text of an error code (negative or not) */
u16  utoa_(u16 v, char *s, u8 base);
int  atoi_(const char *s);
int  isdigit_(int c);

extern int argc_;
extern char *argv_[];

#endif
