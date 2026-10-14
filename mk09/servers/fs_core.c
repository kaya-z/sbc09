/* SPDX-License-Identifier: MIT */
/*
 * fs_core.c - FAT16 file system (8.3 names, directories, read/write)
 *
 * Reads the first FAT16 partition of the MBR (or a FAT16 volume that starts at block 0),
 * keeps three 512 byte blocks in a cache (write through: every change is on the disk when the
 * call returns, except the size and first cluster of an open file, written at close and sync).
 * Long file names are not created; entries of other systems that use them are skipped, their
 * 8.3 alias works.  The current directory is common to all tasks.
 *
 * Volumes have at most 65535 blocks (32MB), so block numbers are 16 bit.  A position in a file
 * is (block, offset in the block); a size likewise.  This keeps the 8 bit CPU away from 32 bit
 * arithmetic, which costs a lot of code with a C compiler.
 *
 * Portable C: only the fsx_* services of fs_core.h reach out of this file.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "fs_core.h"
#include "mkstr.h"

#define NBUF        2
#define BLKBITS     9

typedef struct cbuf {
    u16 blk;                        /* volume relative block number */
    u16 stamp;
    u8  valid;
    u8  data[BLKSIZE];
} cbuf_t;

typedef struct dloc {               /* where a directory entry lives */
    u16 blk;
    u8  idx;
} dloc_t;

typedef struct diter {
    u16 dcl;                        /* the directory (0 = root) */
    u16 cl;                         /* current cluster */
    u16 sec;                        /* block within the cluster / root */
    u8  idx;                        /* next entry in the block */
} diter_t;

typedef struct fh {
    u8  used;                       /* 0 free, 1 file, 2 directory */
    u8  owner;
    u8  flags;
    u8  dirty;
    u16 first;                      /* first cluster (0 = empty file) */
    u16 sblk, soff;                 /* size */
    u16 pblk, poff;                 /* position */
    u16 curcl;                      /* cluster that holds file block cblk */
    u16 cblk;
    u16 contig;                     /* the first 'contig' blocks lie in consecutive clusters (first, first+1, ..) */
    dloc_t loc;                     /* the directory entry */
    diter_t it;                     /* directories: iterator */
} fh_t;

typedef struct res {                /* result of resolving a path */
    u8  found;
    u8  isdirobj;                   /* the path names a directory itself ("/", ".", "..", "a/b/") */
    u16 dcl;                        /* directory that holds (or would hold) the last component */
    u8  name[11];
    u8  ent[32];                    /* the entry (found) */
    dloc_t loc;
} res_t;

static cbuf_t cache[NBUF];
static u16 stamp;
static fh_t fh[NHANDLE];

/* how far a file is known to lie in consecutive clusters, by the number of its first cluster:
 * jwp opens its dictionary again for every lookup, and the walk through the FAT chain of a file of
 * megabytes would be repeated each time */
#define NCC         2
static u16 cc_first[NCC], cc_len[NCC];
static u8  cc_next;

/* geometry */
static u8  mounted;
u16 fs_part_hi, fs_part_lo;
static u8  spc;                     /* blocks per cluster */
static u8  spcshift;                /* log2(spc) */
static u16 rsvd;
static u8  nfats;
static u16 fatsz;
static u16 fat_blk, root_blk, data_blk;
static u16 rootsecs;
static u16 nclusters;               /* data clusters; cluster numbers are 2 .. nclusters+1 */
static u16 free_hint;

/* the current directory */
static u16  cwd_cl;
static char cwd_path[MAXPATH];
static char scratch[MAXPATH + MAXPATH];

/* ------------------------------------------------------------------ */
/* little endian fields                                                */
/* ------------------------------------------------------------------ */
static u16 rd16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

/* ------------------------------------------------------------------ */
/* block cache                                                         */
/* ------------------------------------------------------------------ */
/* mode 0: read the block, 1: the caller overwrites all of it, 2: a block of zeroes */
static cbuf_t *blk_get(u16 blk, u8 mode)
{
    u16 oldest = 0, age;
    cbuf_t *b, *victim = cache;

    for (b = cache; b < cache + NBUF; b++) {
        if (b->valid && b->blk == blk) {
            b->stamp = ++stamp;
            if (mode == 2) memset(b->data, 0, BLKSIZE);
            return b;
        }
    }
    for (b = cache; b < cache + NBUF; b++) {
        if (!b->valid) { victim = b; break; }
        age = stamp - b->stamp;
        if (age >= oldest) { oldest = age; victim = b; }
    }
    b = victim;
    b->valid = 0;
    if (mode == 0) {
        if (fsx_disk(0, blk, b->data) != E_OK) return NULL;
    } else if (mode == 2) {
        memset(b->data, 0, BLKSIZE);
    }
    b->blk = blk;
    b->valid = 1;
    b->stamp = ++stamp;
    return b;
}

static u8 blk_put(cbuf_t *b)
{
    return fsx_disk(1, b->blk, b->data);
}

/* ------------------------------------------------------------------ */
/* FAT                                                                 */
/* ------------------------------------------------------------------ */
static u16 clus_blk(u16 cl)
{
    return data_blk + ((cl - 2) << spcshift);
}

static u16 cc_get(u16 first)
{
    u8 i;
    for (i = 0; i < NCC; i++)
        if (cc_first[i] == first) return cc_len[i];
    return 0;
}

static void cc_put(u16 first, u16 len)
{
    u8 i;
    for (i = 0; i < NCC; i++)
        if (cc_first[i] == first) { cc_len[i] = len; return; }
    cc_first[cc_next] = first;
    cc_len[cc_next] = len;
    cc_next = (cc_next + 1) % NCC;
}

static void cc_drop(u16 first)
{
    u8 i;
    for (i = 0; i < NCC; i++)
        if (cc_first[i] == first) cc_first[i] = 0;
}

static u8 cl_ok(u16 cl)
{
    return cl >= 2 && cl < nclusters + 2;
}

static u16 fat_get(u16 cl)
{
    cbuf_t *b = blk_get(fat_blk + (cl >> 8), 0);
    if (!b) return 0xFFF7;
    return rd16(b->data + (cl & 255) * 2);
}

static u8 fat_set(u16 cl, u16 v)
{
    cbuf_t *b = blk_get(fat_blk + (cl >> 8), 0);
    u8 k;
    if (!b) return E_IO;
    wr16(b->data + (cl & 255) * 2, v);
    if (blk_put(b)) return E_IO;
    for (k = 1; k < nfats; k++)
        if (fsx_disk(1, b->blk + fatsz * k, b->data)) return E_IO;
    return E_OK;
}

/* a free cluster, marked as end of chain and linked behind prev (0: none); 0 = disk full */
static u16 alloc_cluster(u16 prev)
{
    u16 cl, n;
    if (free_hint < 2 || free_hint >= nclusters + 2) free_hint = 2;
    cl = free_hint;
    for (n = 0; n < nclusters; n++) {
        if (fat_get(cl) == 0) {
            if (fat_set(cl, 0xFFFF)) return 0;
            if (prev && fat_set(prev, cl)) return 0;
            free_hint = cl + 1;
            return cl;
        }
        if (++cl >= nclusters + 2) cl = 2;
    }
    return 0;
}

static void free_chain(u16 cl)
{
    u16 nx;
    cc_drop(cl);
    while (cl_ok(cl)) {
        nx = fat_get(cl);
        fat_set(cl, 0);
        if (cl < free_hint) free_hint = cl;
        cl = nx;
    }
}

/* ------------------------------------------------------------------ */
/* directories                                                         */
/* ------------------------------------------------------------------ */
static void dir_begin(diter_t *it, u16 dcl)
{
    it->dcl = it->cl = dcl;
    it->sec = 0;
    it->idx = 0;
}

/* the next raw entry (also deleted ones); 0 at the end of the directory (or on an error) */
static u8 dir_next(diter_t *it, u8 *ent, dloc_t *loc)
{
    u16 blk;
    cbuf_t *b;

    if (it->dcl == 0) {
        if (it->sec >= rootsecs) return 0;
        blk = root_blk + it->sec;
    } else {
        if (!cl_ok(it->cl)) return 0;
        blk = clus_blk(it->cl) + it->sec;
    }
    b = blk_get(blk, 0);
    if (!b) return 0;
    memcpy(ent, b->data + ((u16)it->idx << 5), 32);
    if (loc) { loc->blk = blk; loc->idx = it->idx; }
    if (++it->idx == 16) {
        it->idx = 0;
        it->sec++;
        if (it->dcl != 0 && it->sec >= spc) {
            it->sec = 0;
            it->cl = fat_get(it->cl);
        }
    }
    return 1;
}

static u8 dir_put(const dloc_t *loc, const u8 *ent)
{
    cbuf_t *b = blk_get(loc->blk, 0);
    if (!b) return E_IO;
    memcpy(b->data + ((u16)loc->idx << 5), ent, 32);
    return blk_put(b);
}

/* mark the entry at loc deleted */
static u8 dir_del(const dloc_t *loc)
{
    cbuf_t *b = blk_get(loc->blk, 0);
    if (!b) return E_IO;
    b->data[((u16)loc->idx << 5)] = 0xE5;
    return blk_put(b);
}

static u8 is_real(const u8 *ent)    /* a file or directory entry (not deleted, LFN or label) */
{
    return ent[0] != 0xE5 && ent[11] != 0x0F && !(ent[11] & FA_VOLUME);
}

/* look for a name in a directory */
static u8 dir_find(u16 dcl, const u8 *name, u8 *ent, dloc_t *loc)
{
    diter_t it;
    dir_begin(&it, dcl);
    while (dir_next(&it, ent, loc)) {
        if (ent[0] == 0) return 0;
        if (is_real(ent) && memcmp(ent, name, 11) == 0) return 1;
    }
    return 0;
}

/* add an entry: a free slot of the directory, which grows if it must */
static u8 dir_add(u16 dcl, const u8 *ent, dloc_t *loc)
{
    diter_t it;
    u8 e[32];
    dloc_t l;
    u16 last, nc;
    u8 i;

    dir_begin(&it, dcl);
    while (dir_next(&it, e, &l)) {
        if (e[0] == 0 || e[0] == 0xE5) {
            if (loc) *loc = l;
            return dir_put(&l, ent);
        }
    }
    if (dcl == 0) return E_NOSPC;               /* the root directory has a fixed size */
    last = dcl;                                 /* append a cleared cluster to the chain */
    while (cl_ok(nc = fat_get(last))) last = nc;
    nc = alloc_cluster(last);
    if (!nc) return E_NOSPC;
    for (i = 0; i < spc; i++) {
        cbuf_t *b = blk_get(clus_blk(nc) + i, 2);
        if (!b || blk_put(b)) return E_IO;
    }
    l.blk = clus_blk(nc);
    l.idx = 0;
    if (loc) *loc = l;
    return dir_put(&l, ent);
}

static u8 dir_is_empty(u16 dcl)
{
    diter_t it;
    u8 e[32];
    dir_begin(&it, dcl);
    while (dir_next(&it, e, NULL)) {
        if (e[0] == 0) return 1;
        if (is_real(e) && e[0] != '.') return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* names and paths                                                     */
/* ------------------------------------------------------------------ */
static u8 name_char_ok(u8 c)
{
    if (c >= 0x80) return 1;
    if (c <= ' ' || c == 0x7F) return 0;
    return strchr("\"*+,/:;<=>?[\\]|", c) == NULL;
}

/* "name.ext" -> 11 bytes "NAME    EXT" (n characters at p) */
static u8 to_name11(const char *p, u8 n, u8 *name)
{
    u8 i, k = 0, c, ext = 0;
    memset(name, ' ', 11);
    if (n == 0) return E_NAME;
    for (i = 0; i < n; i++) {
        c = (u8)p[i];
        if (c == '.') {
            if (ext || k == 0) return E_NAME;
            ext = 1;
            k = 8;
            continue;
        }
        if (!name_char_ok(c)) return E_NAME;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (k >= (ext ? 11 : 8)) return E_NAME;
        name[k++] = c;
    }
    if (name[0] == 0xE5) name[0] = 0x05;
    return E_OK;
}

/* "NAME    EXT" -> "NAME.EXT" */
static void from_name11(const u8 *name, char *out)
{
    u8 i, n = 0;
    for (i = 0; i < 8 && name[i] != ' '; i++) out[n++] = (char)name[i];
    if (name[8] != ' ') {
        out[n++] = '.';
        for (i = 8; i < 11 && name[i] != ' '; i++) out[n++] = (char)name[i];
    }
    if (out[0] == 0x05) out[0] = (char)0xE5;
    out[n] = 0;
}

static u16 ent_cluster(const u8 *ent) { return rd16(ent + 26); }

static void now_fields(u8 *ent)
{
    u16 d, t;
    fsx_now(&d, &t);
    wr16(ent + 22, t);
    wr16(ent + 24, d);
}

/* the 32 bit size of a directory entry as (blocks, rest) and back */
static void size_get(const u8 *ent, u16 *blk, u16 *off)
{
    u16 hi = rd16(ent + 30), lo = rd16(ent + 28);
    *blk = (hi << 7) | (lo >> BLKBITS);
    *off = lo & (BLKSIZE - 1);
}

static void size_put(u8 *ent, u16 blk, u16 off)
{
    wr16(ent + 30, blk >> 7);
    wr16(ent + 28, ((blk & 127) << BLKBITS) | off);
}

/* make a directory object result */
static void dirobj(res_t *r, u16 cl)
{
    r->found = 1;
    r->isdirobj = 1;
    r->dcl = cl;
    memset(r->ent, 0, 32);
    r->ent[11] = FA_DIR;
    wr16(r->ent + 26, cl);
}

static u8 resolve(const char *path, res_t *r)
{
    u16 cur = cwd_cl;
    const char *p = path;
    const char *q, *t;
    u8 n, last, st;
    u8 nm[11];
    u8 e[32];

    memset(r, 0, sizeof(res_t));
    if (*p == '/') cur = 0;
    for (;;) {
        while (*p == '/') p++;
        if (*p == 0) { dirobj(r, cur); return E_OK; }
        q = p;
        while (*q && *q != '/') q++;
        n = (u8)(q - p);
        t = q;
        while (*t == '/') t++;
        last = (*t == 0);
        if (n == 1 && p[0] == '.') {
            p = q;
            continue;
        }
        if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (cur != 0) {
                memset(nm, ' ', 11);
                nm[0] = nm[1] = '.';
                if (!dir_find(cur, nm, e, NULL)) return E_IO;
                cur = ent_cluster(e);
            }
            p = q;
            continue;
        }
        st = to_name11(p, n, nm);
        if (st) return st;
        if (last) {
            r->dcl = cur;
            memcpy(r->name, nm, 11);
            r->found = dir_find(cur, nm, r->ent, &r->loc);
            return E_OK;
        }
        if (!dir_find(cur, nm, e, NULL)) return E_NOENT;
        if (!(e[11] & FA_DIR)) return E_NOTDIR;
        cur = ent_cluster(e);
        p = q;
    }
}

/* ------------------------------------------------------------------ */
/* mount                                                               */
/* ------------------------------------------------------------------ */
u8 fs_mount(void)
{
    cbuf_t *b;
    u8 i;
    u16 tot;

    memset(cache, 0, sizeof(cache));
    memset(fh, 0, sizeof(fh));
    memset(cc_first, 0, sizeof(cc_first));
    mounted = 0;
    fs_part_hi = fs_part_lo = 0;
    b = blk_get(0, 0);
    if (!b) return E_IO;
    if (rd16(b->data + 510) != 0xAA55) return E_FORMAT;
    if (!(b->data[0] == 0xEB || b->data[0] == 0xE9)) {       /* not a boot sector: partition table */
        for (i = 0; i < 4; i++) {
            u8 *pe = b->data + 0x1BE + i * 16;
            u8 t = pe[4];
            if (t == 0x04 || t == 0x06 || t == 0x0E) {
                fs_part_lo = rd16(pe + 8);
                fs_part_hi = rd16(pe + 10);
                break;
            }
        }
        if (i == 4) return E_FORMAT;
        memset(cache, 0, sizeof(cache));
        b = blk_get(0, 0);
        if (!b) return E_IO;
        if (rd16(b->data + 510) != 0xAA55) return E_FORMAT;
    }
    if (rd16(b->data + 11) != BLKSIZE) return E_FORMAT;
    spc = b->data[13];
    rsvd = rd16(b->data + 14);
    nfats = b->data[16];
    tot = rd16(b->data + 17);                               /* root entries */
    rootsecs = (tot + 15) >> 4;
    tot = rd16(b->data + 19);
    if (tot == 0) {
        if (rd16(b->data + 34)) return E_FORMAT;            /* more than 32MB */
        tot = rd16(b->data + 32);
    }
    fatsz = rd16(b->data + 22);
    if (spc == 0 || nfats == 0 || nfats > 2 || fatsz == 0 || rootsecs == 0) return E_FORMAT;
    for (spcshift = 0; (1 << spcshift) < spc; spcshift++)
        ;
    if ((1 << spcshift) != spc) return E_FORMAT;                /* clusters of 2^n blocks */
    fat_blk = rsvd;
    root_blk = fat_blk + fatsz * nfats;
    data_blk = root_blk + rootsecs;
    if (tot <= data_blk) return E_FORMAT;
    tot = (tot - data_blk) / spc;
    if (tot < 4085 || tot >= 65525) return E_FORMAT;         /* not FAT16 */
    nclusters = tot;
    free_hint = 2;
    cwd_cl = 0;
    cwd_path[0] = '/';
    cwd_path[1] = 0;
    mounted = 1;
    return E_OK;
}

#define NEED_MOUNT  if (!mounted) return E_FORMAT

/* ------------------------------------------------------------------ */
/* handles                                                             */
/* ------------------------------------------------------------------ */
static fh_t *hget(u8 h, u8 kind)
{
    if (h >= NHANDLE || fh[h].used != kind) return NULL;
    return &fh[h];
}

/* write the size and first cluster of a file to its directory entry */
static u8 flush_fh(fh_t *f)
{
    cbuf_t *b;
    u8 *e;
    if (!f->dirty) return E_OK;
    b = blk_get(f->loc.blk, 0);
    if (!b) return E_IO;
    e = b->data + ((u16)f->loc.idx << 5);
    wr16(e + 26, f->first);
    size_put(e, f->sblk, f->soff);
    now_fields(e);
    e[11] |= FA_ARCHIVE;
    f->dirty = 0;
    return blk_put(b);
}

u8 fs_open(const char *path, u8 flags, u8 owner, u8 *h)
{
    res_t r;
    fh_t *f = NULL;
    u8 i, st;
    u8 e[32];

    NEED_MOUNT;
    for (i = 0; i < NHANDLE; i++)
        if (!fh[i].used) { f = &fh[i]; break; }
    if (!f) return E_MFILE;
    st = resolve(path, &r);
    if (st) return st;
    memset(f, 0, sizeof(fh_t));
    f->owner = owner;
    f->flags = flags;
    if (r.isdirobj || (r.found && (r.ent[11] & FA_DIR))) {
        if (!(flags & FO_DIR) || (flags & (FO_WRITE | FO_CREATE | FO_TRUNC))) return E_ISDIR;
        f->used = 2;
        dir_begin(&f->it, ent_cluster(r.ent));
        *h = i;
        return E_OK;
    }
    if (flags & FO_DIR) return E_NOTDIR;
    if (!r.found) {
        if (!(flags & FO_CREATE)) return E_NOENT;
        memset(e, 0, 32);
        memcpy(e, r.name, 11);
        e[11] = FA_ARCHIVE;
        now_fields(e);
        wr16(e + 14, rd16(e + 22));
        wr16(e + 16, rd16(e + 24));
        wr16(e + 18, rd16(e + 24));
        st = dir_add(r.dcl, e, &f->loc);
        if (st) return st;
    } else {
        if (flags & FO_EXCL) return E_EXIST;
        if ((flags & (FO_WRITE | FO_TRUNC)) && (r.ent[11] & FA_RDONLY)) return E_RO;
        f->loc = r.loc;
        f->first = ent_cluster(r.ent);
        f->contig = cc_get(f->first);
        size_get(r.ent, &f->sblk, &f->soff);
        if (flags & FO_TRUNC) {
            if (f->first) free_chain(f->first);
            f->first = 0;
            f->sblk = f->soff = 0;
            f->dirty = 1;
        }
    }
    f->used = 1;
    if (f->dirty) flush_fh(f);
    if (flags & FO_APPEND) { f->pblk = f->sblk; f->poff = f->soff; }
    *h = i;
    return E_OK;
}

u8 fs_close(u8 h)
{
    fh_t *f;
    u8 st = E_OK;
    if (h >= NHANDLE || !fh[h].used) return E_BADF;
    f = &fh[h];
    if (f->used == 1) st = flush_fh(f);
    f->used = 0;
    return st;
}

void fs_closeall(u8 owner)
{
    u8 i;
    for (i = 0; i < NHANDLE; i++)
        if (fh[i].used && fh[i].owner == owner) fs_close(i);
}

u8 fs_chown(u8 h, u8 owner)
{
    if (h >= NHANDLE || !fh[h].used) return E_BADF;
    fh[h].owner = owner;
    return E_OK;
}

u8 fs_sync(void)
{
    u8 i, st = E_OK;
    for (i = 0; i < NHANDLE; i++)
        if (fh[i].used == 1 && flush_fh(&fh[i])) st = E_IO;
    return st;
}

/* make f->curcl the cluster that holds the block f->pblk; grow = allocate clusters at the end.
 * Files made by mkdisk.py and by this server lie in consecutive clusters: f->contig remembers how far
 * that is known, and a position inside it is found without walking the FAT chain. */
static u8 seek_cluster(fh_t *f, u8 grow)
{
    u16 nx;

    if (f->first == 0) {
        if (!grow) return E_EOF;
        nx = alloc_cluster(0);
        if (!nx) return E_NOSPC;
        f->first = nx;
        f->dirty = 1;
        f->curcl = 0;
        f->contig = 0;
    }
    if (f->pblk < f->contig) {
        f->curcl = f->first + (f->pblk >> spcshift);
        f->cblk = (f->pblk >> spcshift) << spcshift;
        return E_OK;
    }
    if (f->curcl == 0 || f->pblk < f->cblk) {
        f->curcl = f->first;
        f->cblk = 0;
        if (f->contig == 0) f->contig = spc;
    }
    while ((u16)(f->pblk - f->cblk) >= spc) {
        nx = fat_get(f->curcl);
        if (!cl_ok(nx)) {
            if (!grow) return E_EOF;
            nx = alloc_cluster(f->curcl);
            if (!nx) return E_NOSPC;
        }
        if (nx == f->curcl + 1 && f->cblk + spc == f->contig) {
            f->contig += spc;
            cc_put(f->first, f->contig);
        }
        f->curcl = nx;
        f->cblk += spc;
    }
    return E_OK;
}

u8 fs_read(u8 h, u8 client, u16 n, u16 *got)
{
    fh_t *f = hget(h, 1);
    u16 done = 0, chunk, left;
    u8 st = E_OK;
    cbuf_t *b;

    *got = 0;
    if (!f) return E_BADF;
    if (f->pblk > f->sblk || (f->pblk == f->sblk && f->poff >= f->soff)) return E_OK;
    left = f->sblk - f->pblk;                   /* bytes left, if less than 64K */
    if (left < 128) {
        left = (left << BLKBITS) + f->soff - f->poff;
        if (left < n) n = left;
    }
    while (done < n) {
        st = seek_cluster(f, 0);
        if (st) break;
        b = blk_get(clus_blk(f->curcl) + (f->pblk - f->cblk), 0);
        if (!b) { st = E_IO; break; }
        chunk = BLKSIZE - f->poff;
        if (chunk > n - done) chunk = n - done;
        st = fsx_put(client, done, b->data + f->poff, chunk);
        if (st) break;
        done += chunk;
        f->poff += chunk;
        if (f->poff >= BLKSIZE) { f->poff -= BLKSIZE; f->pblk++; }
    }
    *got = done;
    return done ? E_OK : st;
}

u8 fs_write(u8 h, u8 client, u16 n, u16 *got)
{
    fh_t *f = hget(h, 1);
    u16 done = 0, chunk;
    u8 st = E_OK, mode;
    cbuf_t *b;

    *got = 0;
    if (!f) return E_BADF;
    if (!(f->flags & FO_WRITE)) return E_RO;
    if (f->flags & FO_APPEND) { f->pblk = f->sblk; f->poff = f->soff; }
    while (done < n) {
        st = seek_cluster(f, 1);
        if (st) break;
        chunk = BLKSIZE - f->poff;
        if (chunk > n - done) chunk = n - done;
        mode = 0;
        if (chunk == BLKSIZE) mode = 1;
        else if (f->pblk > f->sblk || (f->pblk == f->sblk && f->soff == 0)) mode = 2;   /* beyond the end: stale data in the cluster */
        b = blk_get(clus_blk(f->curcl) + (f->pblk - f->cblk), mode);
        if (!b) { st = E_IO; break; }
        st = fsx_get(client, done, b->data + f->poff, chunk);
        if (st) break;
        if (blk_put(b)) { st = E_IO; break; }
        done += chunk;
        f->poff += chunk;
        if (f->poff >= BLKSIZE) { f->poff -= BLKSIZE; f->pblk++; }
        if (f->pblk > f->sblk || (f->pblk == f->sblk && f->poff > f->soff)) {
            f->sblk = f->pblk;
            f->soff = f->poff;
            f->dirty = 1;
        }
    }
    *got = done;
    return done ? E_OK : st;
}

/* (hi, lo) as byte count <-> (block, offset) */
u8 fs_seek(u8 h, u16 hi, u16 lo, u8 whence, u16 *rhi, u16 *rlo)
{
    fh_t *f = hget(h, 1);
    u16 bhi, blo, nhi, nlo;
    u16 blk, off;

    if (!f) return E_BADF;
    if (whence == 0) {
        bhi = blo = 0;
    } else if (whence == 1) {
        bhi = f->pblk >> 7;
        blo = ((f->pblk & 127) << BLKBITS) | f->poff;
    } else if (whence == 2) {
        bhi = f->sblk >> 7;
        blo = ((f->sblk & 127) << BLKBITS) | f->soff;
    } else {
        return E_INVAL;
    }
    nlo = blo + lo;
    nhi = bhi + hi + (nlo < lo);
    if (nhi & 0x8000) return E_INVAL;                       /* before the start */
    blk = (nhi << 7) | (nlo >> BLKBITS);
    off = nlo & (BLKSIZE - 1);
    if (nhi > 0x1FF || blk > f->sblk || (blk == f->sblk && off > f->soff)) {
        blk = f->sblk;                                      /* no holes: stop at the end */
        off = f->soff;
    }
    f->pblk = blk;
    f->poff = off;
    *rhi = blk >> 7;
    *rlo = ((blk & 127) << BLKBITS) | off;
    return E_OK;
}

u8 fs_fstat(u8 h, u8 *attr, u16 *shi, u16 *slo)
{
    fh_t *f;
    if (h >= NHANDLE || !fh[h].used) return E_BADF;
    f = &fh[h];
    *attr = FA_ARCHIVE;
    *shi = *slo = 0;
    if (f->used == 2) {
        *attr = FA_DIR;
    } else {
        *shi = f->sblk >> 7;
        *slo = ((f->sblk & 127) << BLKBITS) | f->soff;
    }
    return E_OK;
}

u8 fs_stat(const char *path, u8 *attr, u16 *shi, u16 *slo, u16 *date)
{
    res_t r;
    u8 st;
    NEED_MOUNT;
    st = resolve(path, &r);
    if (st) return st;
    if (!r.found) return E_NOENT;
    *attr = r.ent[11];
    *shi = *slo = 0;
    if (!(r.ent[11] & FA_DIR)) {
        *shi = rd16(r.ent + 30);
        *slo = rd16(r.ent + 28);
    }
    *date = rd16(r.ent + 24);
    return E_OK;
}

u8 fs_readdir(u8 h, dirent_t *d)
{
    fh_t *f = hget(h, 2);
    u8 e[32];
    if (!f) return E_BADF;
    while (dir_next(&f->it, e, NULL)) {
        if (e[0] == 0) return E_EOF;
        if (!is_real(e)) continue;
        from_name11(e, d->name);
        d->attr = e[11];
        d->sizehi = d->sizelo = 0;
        if (!(e[11] & FA_DIR)) {
            d->sizehi = rd16(e + 30);
            d->sizelo = rd16(e + 28);
        }
        d->date = rd16(e + 24);
        d->time = rd16(e + 22);
        return E_OK;
    }
    return E_EOF;
}

u8 fs_mkdir(const char *path)
{
    res_t r;
    u8 e[32];
    u16 cl;
    u8 st, i;
    cbuf_t *b;

    NEED_MOUNT;
    st = resolve(path, &r);
    if (st) return st;
    if (r.found) return E_EXIST;
    cl = alloc_cluster(0);
    if (!cl) return E_NOSPC;
    for (i = 0; i < spc; i++) {
        b = blk_get(clus_blk(cl) + i, 2);
        if (!b) return E_IO;
        if (i == 0) {
            memset(e, 0, 32);
            memset(e, ' ', 11);
            e[0] = '.';
            e[11] = FA_DIR;
            now_fields(e);
            wr16(e + 26, cl);
            memcpy(b->data, e, 32);
            e[1] = '.';
            wr16(e + 26, r.dcl);
            memcpy(b->data + 32, e, 32);
        }
        if (blk_put(b)) return E_IO;
    }
    memset(e, 0, 32);
    memcpy(e, r.name, 11);
    e[11] = FA_DIR;
    now_fields(e);
    wr16(e + 26, cl);
    st = dir_add(r.dcl, e, NULL);
    if (st) free_chain(cl);
    return st;
}

u8 fs_remove(const char *path)
{
    res_t r;
    u8 st, i;

    NEED_MOUNT;
    st = resolve(path, &r);
    if (st) return st;
    if (r.isdirobj) return E_INVAL;
    if (!r.found) return E_NOENT;
    if (r.ent[11] & FA_RDONLY) return E_RO;
    if ((r.ent[11] & FA_DIR) && !dir_is_empty(ent_cluster(r.ent))) return E_NOTEMPTY;
    for (i = 0; i < NHANDLE; i++)                   /* open handles of the file */
        if (fh[i].used == 1 && fh[i].loc.blk == r.loc.blk && fh[i].loc.idx == r.loc.idx)
            fh[i].used = 0;
    st = dir_del(&r.loc);
    if (st) return st;
    if (ent_cluster(r.ent)) free_chain(ent_cluster(r.ent));
    return E_OK;
}

/* is directory 'anc' the directory dcl or one of its parents? (for rename) */
static u8 is_ancestor(u16 anc, u16 dcl)
{
    u8 nm[11], e[32];
    u8 depth = 0;
    memset(nm, ' ', 11);
    nm[0] = nm[1] = '.';
    while (dcl != 0 && depth++ < 100) {
        if (dcl == anc) return 1;
        if (!dir_find(dcl, nm, e, NULL)) return 0;
        dcl = ent_cluster(e);
    }
    return 0;
}

u8 fs_rename(const char *from, const char *to)
{
    res_t a, c;
    u8 st, e[32];
    cbuf_t *b;

    NEED_MOUNT;
    st = resolve(from, &a);
    if (st) return st;
    if (a.isdirobj) return E_INVAL;
    if (!a.found) return E_NOENT;
    st = resolve(to, &c);
    if (st) return st;
    if (c.isdirobj || c.found) return E_EXIST;
    if ((a.ent[11] & FA_DIR) && is_ancestor(ent_cluster(a.ent), c.dcl)) return E_INVAL;
    memcpy(e, a.ent, 32);
    memcpy(e, c.name, 11);
    st = dir_add(c.dcl, e, NULL);
    if (st) return st;
    st = dir_del(&a.loc);
    if (st) return st;
    if ((a.ent[11] & FA_DIR) && c.dcl != a.dcl) {   /* '..' of the moved directory */
        b = blk_get(clus_blk(ent_cluster(a.ent)), 0);
        if (!b) return E_IO;
        wr16(b->data + 32 + 26, c.dcl);
        if (blk_put(b)) return E_IO;
    }
    return E_OK;
}

/* ------------------------------------------------------------------ */
/* current directory                                                   */
/* ------------------------------------------------------------------ */
/* the textual absolute path of 'path' (. and .. removed) into out */
static u8 norm_path(const char *path, char *out)
{
    char *tmp = scratch;
    u8 n, i, len;
    const char *p, *q;

    tmp[0] = 0;
    if (*path != '/') strcpy(tmp, cwd_path);
    n = (u8)strlen(tmp);
    if (n == 1) n = 0;                              /* "/" */
    p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        q = p;
        while (*q && *q != '/') q++;
        len = (u8)(q - p);
        if (len == 1 && p[0] == '.') {
            /* nothing */
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            while (n && tmp[n - 1] != '/') n--;
            if (n) n--;
        } else {
            if (n + 1 + len >= MAXPATH) return E_NAME;
            tmp[n++] = '/';
            for (i = 0; i < len; i++) {
                char c = p[i];
                if (c >= 'a' && c <= 'z') c -= 32;
                tmp[n++] = c;
            }
        }
        p = q;
    }
    if (n == 0) { out[0] = '/'; out[1] = 0; }
    else { memcpy(out, tmp, n); out[n] = 0; }
    return E_OK;
}

u8 fs_chdir(const char *path)
{
    res_t r;
    char np[MAXPATH];
    u8 st;
    NEED_MOUNT;
    st = resolve(path, &r);
    if (st) return st;
    if (!r.found) return E_NOENT;
    if (!(r.ent[11] & FA_DIR)) return E_NOTDIR;
    st = norm_path(path, np);
    if (st) return st;
    cwd_cl = ent_cluster(r.ent);
    strcpy(cwd_path, np);
    return E_OK;
}

u8 fs_getcwd(char *buf, u16 n)
{
    u16 l = (u16)strlen(cwd_path) + 1;
    if (l > n) return E_INVAL;
    memcpy(buf, cwd_path, l);
    return E_OK;
}

u8 fs_statfs(u8 *spcp, u16 *clusters, u16 *freec)
{
    u16 cl, n = 0, blk, i, last = nclusters + 2;
    cbuf_t *b;
    NEED_MOUNT;
    for (blk = 0; blk < fatsz && (u16)(blk << 8) < last; blk++) {
        b = blk_get(fat_blk + blk, 0);
        if (!b) return E_IO;
        for (i = 0; i < 256; i++) {
            cl = (blk << 8) + i;
            if (cl >= 2 && cl < last && rd16(b->data + i * 2) == 0) n++;
        }
    }
    *spcp = spc;
    *clusters = nclusters;
    *freec = n;
    return E_OK;
}
