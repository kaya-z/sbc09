/* SPDX-License-Identifier: MIT */
/*
 * fs_core.h - the FAT16 file system code of the FS server
 *
 * fs_core.c does not know about messages: it needs four services from its environment
 * (the glue fs.c on MK09, or the test program tests/fs_host.c on the host).
 *
 * Limits (they keep the code small on an 8 bit CPU): the volume has at most 65535 blocks of
 * 512 bytes (32MB) so that block numbers are 16 bit; sizes and positions are 32 bit values
 * passed as (hi, lo) 16 bit pairs.
 */
#ifndef FS_CORE_H
#define FS_CORE_H

#include "types.h"
#include "proto.h"

/* read (write = 0) or write one 512 byte block (blk is relative to the start of the
 * volume: add fs_part_hi:fs_part_lo); returns E_OK or E_IO */
u8   fsx_disk(u8 write, u16 blk, u8 *buf);
/* data of the client's granted buffer (offset from its start) */
u8   fsx_get(u8 client, u16 off, void *dst, u16 n);
u8   fsx_put(u8 client, u16 off, const void *src, u16 n);
/* the time in FAT format */
void fsx_now(u16 *date, u16 *time);

extern u16 fs_part_hi, fs_part_lo;     /* first block of the partition (set by fs_mount) */

#define NHANDLE     6

u8   fs_mount(void);
u8   fs_open(const char *path, u8 flags, u8 owner, u8 *h);
u8   fs_close(u8 h);
u8   fs_read(u8 h, u8 client, u16 n, u16 *got);
u8   fs_write(u8 h, u8 client, u16 n, u16 *got);
/* whence 0: from the start, 1: from here, 2: from the end; (hi, lo) is signed */
u8   fs_seek(u8 h, u16 hi, u16 lo, u8 whence, u16 *rhi, u16 *rlo);
u8   fs_fstat(u8 h, u8 *attr, u16 *shi, u16 *slo);
u8   fs_stat(const char *path, u8 *attr, u16 *shi, u16 *slo, u16 *date);
u8   fs_readdir(u8 h, dirent_t *d);
u8   fs_mkdir(const char *path);
u8   fs_remove(const char *path);
u8   fs_rename(const char *from, const char *to);
u8   fs_chdir(const char *path);
u8   fs_getcwd(char *buf, u16 n);
u8   fs_sync(void);
void fs_closeall(u8 owner);
u8   fs_chown(u8 h, u8 owner);
u8   fs_statfs(u8 *spc, u16 *clusters, u16 *freec);

#endif
