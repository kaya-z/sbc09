/* SPDX-License-Identifier: MIT */
/*
 * fs.c - the file system server (TASK 3): the message interface to fs_core.c
 *
 * The block device is the DISK server; the data of read and write requests goes between the
 * cache blocks of this server and the client's granted buffer (mk_copyto / mk_copyfrom).
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "bsp.h"
#include "fs_core.h"
#include "mkstr.h"

u8 fsx_disk(u8 write, u16 blk, u8 *buf)
{
    msg_t m;
    u16 lo = fs_part_lo + blk;
    m.op = write ? DISK_WRITE : DISK_READ;
    m.a = lo;
    m.d = (fs_part_hi + (lo < blk)) & 0xFF;     /* drive 0 */
    m.b = (u16)buf;
    m.c = BLKSIZE;
    if (mk_callg(TID_DISK, &m, write ? GR_READ : GR_WRITE)) return E_IO;
    return m.op;
}

u8 fsx_get(u8 client, u16 off, void *dst, u16 n)
{
    return mk_copyfrom(client, off, dst, n);
}

u8 fsx_put(u8 client, u16 off, const void *src, u16 n)
{
    return mk_copyto(client, off, src, n);
}

void fsx_now(u16 *date, u16 *time)
{
    bsp_now_fat(date, time);
}

/* fetch the path argument (b = address, c = length incl. NUL) of a request */
static char path[2 * MAXPATH];          /* the path (or two, for rename) of the request being served */
static dirent_t de;

static u8 get_path(msg_t *m)
{
    if (m->c == 0 || m->c > MAXPATH) return E_NAME;
    if (mk_copyfrom(m->src, 0, path, m->c)) return E_FAULT;
    path[m->c - 1] = 0;
    return E_OK;
}

int main(void)
{
    msg_t m, r;
    u16 got, date, freec, clusters, shi, slo;
    u8 st, h, attr, spc;

    st = fs_mount();
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) continue;
        r.a = r.b = r.c = r.d = 0;
        r.op = E_NOSYS;
        if (st != E_OK && m.op != FS_CLOSEALL) {            /* not mounted: try again */
            st = fs_mount();
            if (st != E_OK) { r.op = st; mk_reply(m.src, &r); continue; }
        }
        switch (m.op) {
        case FS_OPEN:
            r.op = get_path(&m);
            if (r.op) break;
            r.op = fs_open(path, (u8)m.a, m.src, &h);
            r.a = h;
            break;
        case FS_CLOSE:
            r.op = fs_close((u8)m.a);
            break;
        case FS_READ:
            r.op = fs_read((u8)m.a, m.src, m.c, &got);
            r.a = got;
            break;
        case FS_WRITE:
            r.op = fs_write((u8)m.a, m.src, m.c, &got);
            r.a = got;
            break;
        case FS_SEEK:
            r.op = fs_seek((u8)m.a, m.c, m.b, (u8)m.d, &shi, &slo);
            r.b = slo;
            r.c = shi;
            break;
        case FS_STAT:
            r.op = get_path(&m);
            if (r.op) break;
            r.op = fs_stat(path, &attr, &shi, &slo, &date);
            r.a = attr;
            r.b = slo;
            r.c = shi;
            r.d = date;
            break;
        case FS_FSTAT:
            r.op = fs_fstat((u8)m.a, &attr, &shi, &slo);
            r.a = attr;
            r.b = slo;
            r.c = shi;
            break;
        case FS_READDIR:
            if (m.c < sizeof(dirent_t)) { r.op = E_INVAL; break; }
            r.op = fs_readdir((u8)m.a, &de);
            if (r.op == E_OK) {
                r.op = mk_copyto(m.src, 0, &de, sizeof(dirent_t));
                r.a = 1;
            } else if (r.op == E_EOF) {
                r.op = E_OK;
                r.a = 0;
            }
            break;
        case FS_MKDIR:
            r.op = get_path(&m);
            if (!r.op) r.op = fs_mkdir(path);
            break;
        case FS_REMOVE:
            r.op = get_path(&m);
            if (!r.op) r.op = fs_remove(path);
            break;
        case FS_RENAME:                                     /* "old\0new\0" */
            if (m.c < 4 || m.c > 2 * MAXPATH) { r.op = E_NAME; break; }
            if (mk_copyfrom(m.src, 0, path, m.c)) { r.op = E_FAULT; break; }
            path[m.c - 1] = 0;
            {
                char *p2 = path + strlen(path) + 1;
                if (p2 >= path + m.c || strlen(path) >= MAXPATH || strlen(p2) >= MAXPATH) { r.op = E_NAME; break; }
                r.op = fs_rename(path, p2);
            }
            break;
        case FS_CHDIR:
            r.op = get_path(&m);
            if (!r.op) r.op = fs_chdir(path);
            break;
        case FS_GETCWD:
            if (m.c > MAXPATH) m.c = MAXPATH;
            r.op = fs_getcwd(path, m.c);
            if (r.op == E_OK) r.op = mk_copyto(m.src, 0, path, strlen(path) + 1);
            break;
        case FS_SYNC:
            r.op = fs_sync();
            break;
        case FS_CLOSEALL:
            fs_closeall((u8)m.a);
            r.op = E_OK;
            break;
        case FS_STATFS:
            r.op = fs_statfs(&spc, &clusters, &freec);
            r.a = spc;
            r.b = clusters;
            r.c = freec;
            break;
        case FS_CHOWN:
            r.op = fs_chown((u8)m.a, (u8)m.b);
            break;
        }
        mk_reply(m.src, &r);
    }
    return 0;
}
