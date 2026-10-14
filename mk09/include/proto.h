/* SPDX-License-Identifier: MIT */
/*
 * proto.h - request codes of the MK09 servers (TTY, DISK, FS, PM)
 *
 * A request is a msg_t: op = request code, a..d = arguments.  Requests that
 * move bulk data carry the buffer in b (address) and c (length) and are sent
 * with mk_callg(); the server reaches the buffer with mk_copyfrom/mk_copyto.
 * The reply has op = status (E_*), results in a..d.
 */
#ifndef MK_PROTO_H
#define MK_PROTO_H

#include "mk09.h"

/* ---- TTY: the console ------------------------------------------------ */
#define TTY_READ    1       /* b = buf, c = len (GR_WRITE) -> a = bytes read   */
#define TTY_WRITE   2       /* b = buf, c = len (GR_READ)  -> a = bytes written */
#define TTY_IOCTL   3       /* a = TTYC_*, b, c arguments */

#define TTYC_GETMODE 1      /* -> a = TM_* flags, b = charset, c = columns, d = rows */
#define TTYC_SETMODE 2      /* b = TM_* flags, c = charset (0xFFFF: keep), d = 0xFFFF keep size;
                               else d = cols << 8 | rows */
#define TTYC_READY   3      /* -> a = bytes that can be read without waiting */
#define TTYC_FLUSH   4      /* discard the input */

#define TM_RAW      0x01    /* no line editing; read returns what is there */
#define TM_ECHO     0x02    /* echo the input */
#define TM_CRLF     0x04    /* output: LF -> CR LF */
#define TM_NOWAIT   0x08    /* read returns 0 at once when there is no input */
#define TM_ISIG     0x10    /* ^C sends NFY_INT to PM */
#define TM_DEFAULT  (TM_ECHO | TM_CRLF | TM_ISIG)

/* character sets of the console (the number is the one jwp / OS-9 kmode use) */
#define CS_RAW      0       /* single byte characters */
#define CS_UTF8     1
#define CS_EUC      2       /* EUC-JP */
#define CS_SJIS     3       /* Shift_JIS (CP932) */

/* ---- DISK: 512 byte blocks of the SD card (or disk image) ----------------- */
#define DISK_READ   1       /* a = block lo, d = drive << 8 | block hi, b = buf, c = 512 (GR_WRITE) */
#define DISK_WRITE  2       /* same (GR_READ) */
#define DISK_SIZE   3       /* d = drive -> a = 1 if present */
#define BLKSIZE     512

/* ---- FS: FAT16 file system on partition 1 of drive 0 ----------------------- */
#define FS_OPEN     1       /* a = FO_* flags, b = path, c = path length incl. NUL (GR_READ) -> a = handle */
#define FS_CLOSE    2       /* a = handle */
#define FS_READ     3       /* a = handle, b = buf, c = len (GR_WRITE) -> a = bytes (0 = end of file) */
#define FS_WRITE    4       /* a = handle, b = buf, c = len (GR_READ)  -> a = bytes */
#define FS_SEEK     5       /* a = handle, c:b = signed offset (b low), d = 0 start, 1 here, 2 end -> c:b = new position */
#define FS_STAT     6       /* b = path, c = len (GR_READ) -> a = attr, c:b = size (b low), d = date */
#define FS_READDIR  7       /* a = dir handle, b = dirent_t *, c = sizeof (GR_WRITE) -> a = 1 entry, 0 end */
#define FS_MKDIR    8       /* b = path, c = len (GR_READ) */
#define FS_REMOVE   9       /* b = path, c = len: delete a file or an empty directory */
#define FS_RENAME   10      /* b = "old\0new\0", c = len (GR_READ) */
#define FS_CHDIR    11      /* b = path, c = len (GR_READ) */
#define FS_GETCWD   12      /* b = buf, c = len (GR_WRITE) */
#define FS_SYNC     13
#define FS_CLOSEALL 14      /* a = tid: close the handles of an exited task (PM) */
#define FS_STATFS   15      /* -> a = sectors per cluster, b = clusters, c = free clusters (low 16 bits) */
#define FS_FSTAT    16      /* a = handle -> c:b = size, a = attr */
#define FS_CHOWN    17      /* a = handle, b = new owner tid (PM) */

#define FO_READ     0x01
#define FO_WRITE    0x02
#define FO_CREATE   0x04
#define FO_TRUNC    0x08
#define FO_APPEND   0x10
#define FO_DIR      0x20    /* open a directory (for FS_READDIR) */
#define FO_EXCL     0x40    /* with FO_CREATE: fail if the file exists */

#define FA_RDONLY   0x01
#define FA_HIDDEN   0x02
#define FA_SYSTEM   0x04
#define FA_VOLUME   0x08
#define FA_DIR      0x10
#define FA_ARCHIVE  0x20

typedef struct dirent {
    char name[13];          /* "NAME.EXT", NUL terminated */
    u8   attr;
    u16  sizehi, sizelo;
    u16  date, time;        /* FAT format */
} dirent_t;

#define MAXPATH     64
#define NOHANDLE    0xFFFF

/* ---- PM: process manager ---------------------------------------------- */
#define PM_SPAWN    1       /* a = stdin handle, d = stdout handle (NOHANDLE: the console),
                               b = "prog\0arg1\0...\0\0", c = len (GR_READ)
                               The reply comes when the program has exited: a = exit code. */
#define PM_SHUTDOWN 2       /* power off */
#define PM_TIME     3       /* -> a = date, b = time (FAT format) */

#define ARGMAX      128     /* bytes of the argument block */

#endif
