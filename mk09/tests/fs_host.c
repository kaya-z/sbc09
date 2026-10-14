/* SPDX-License-Identifier: MIT */
/*
 * fs_host.c - the FAT16 code of the FS server (servers/fs_core.c) on the host
 *
 * The four services of fs_core.h are a disk image file and a memory buffer that plays the
 * client's granted buffer.  The program runs a list of operations given on the command line
 * or a random test; the image is then checked with fsck.fat by the caller.
 *
 *   fs_host IMAGE ls PATH | cat PATH | put PATH FILE | rm PATH | mkdir PATH | mv A B | stat PATH
 *   fs_host IMAGE random SEED COUNT
 */
#define MK_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "fs_core.h"

static FILE *img;
static u8 cbuf[70000];                 /* the client's buffer */
static long nreads, nwrites;

u8 fsx_disk(u8 write, u16 blk, u8 *buf)
{
    long off = ((long)fs_part_hi << 16 | fs_part_lo) * 512L + (long)blk * 512L;
    if (fseek(img, off, SEEK_SET)) return E_IO;
    if (write) { nwrites++; return fwrite(buf, 1, 512, img) == 512 ? E_OK : E_IO; }
    nreads++;
    return fread(buf, 1, 512, img) == 512 ? E_OK : E_IO;
}

u8 fsx_get(u8 client, u16 off, void *dst, u16 n) { (void)client; memcpy(dst, cbuf + off, n); return E_OK; }
u8 fsx_put(u8 client, u16 off, const void *src, u16 n) { (void)client; memcpy(cbuf + off, src, n); return E_OK; }
void fsx_now(u16 *date, u16 *time) { *date = (44 << 9) | (10 << 5) | 14; *time = (12 << 11) | (30 << 5); }

static int die(const char *what, u8 st)
{
    fprintf(stderr, "%s: error %d\n", what, st);
    return 1;
}

static int do_ls(const char *path)
{
    u8 h, st;
    dirent_t d;
    st = fs_open(path, FO_DIR | FO_READ, 1, &h);
    if (st) return die(path, st);
    while (fs_readdir(h, &d) == E_OK)
        printf("%-13s %02x %lu\n", d.name, d.attr, ((unsigned long)d.sizehi << 16) | d.sizelo);
    fs_close(h);
    return 0;
}

static int do_cat(const char *path, FILE *out)
{
    u8 h, st;
    u16 got;
    st = fs_open(path, FO_READ, 1, &h);
    if (st) return die(path, st);
    for (;;) {
        st = fs_read(h, 1, 4096, &got);
        if (st) return die("read", st);
        if (!got) break;
        fwrite(cbuf, 1, got, out);
    }
    fs_close(h);
    return 0;
}

static int do_put(const char *path, const char *file)
{
    u8 h, st;
    u16 got;
    size_t n;
    FILE *f = fopen(file, "rb");
    if (!f) return die(file, 1);
    st = fs_open(path, FO_WRITE | FO_CREATE | FO_TRUNC, 1, &h);
    if (st) return die(path, st);
    while ((n = fread(cbuf, 1, 3000, f)) > 0) {
        st = fs_write(h, 1, (u16)n, &got);
        if (st || got != n) return die("write", st ? st : E_NOSPC);
    }
    st = fs_close(h);
    fclose(f);
    return st ? die("close", st) : 0;
}

/* ---- a random test against a model in memory --------------------------------------- */
#define NF 6
static char names[NF][16] = { "/A.DAT", "/B.DAT", "/D/C.DAT", "/D/E.DAT", "/D/S/F.DAT", "/G.DAT" };
static unsigned char *model[NF];
static long mlen[NF];
static int exists[NF];

static unsigned rnd(void) { return (unsigned)rand(); }

static int random_test(int seed, int count)
{
    int i, op, k, fails = 0;
    u8 h, st;
    u16 got;
    srand(seed);
    for (i = 0; i < NF; i++) model[i] = calloc(1, 300000);
    if (fs_mkdir("/D")) return die("mkdir /D", 1);
    if (fs_mkdir("/D/S")) return die("mkdir /D/S", 1);
    for (op = 0; op < count; op++) {
        k = rnd() % NF;
        switch (rnd() % 6) {
        case 0: case 1: {                           /* write at the end / replace */
            int trunc = rnd() % 3 == 0;
            long n = 1 + rnd() % 5000;
            unsigned char *tmp = malloc(n);
            long done = 0;
            for (i = 0; i < n; i++) tmp[i] = rnd();
            st = fs_open(names[k], FO_WRITE | FO_CREATE | (trunc || !exists[k] ? FO_TRUNC : FO_APPEND), 1, &h);
            if (st) { fprintf(stderr, "open %s: %d\n", names[k], st); fails++; free(tmp); break; }
            if (trunc || !exists[k]) mlen[k] = 0;
            exists[k] = 1;
            while (done < n) {
                u16 part = 1 + rnd() % 1500;
                if (part > n - done) part = n - done;
                memcpy(cbuf, tmp + done, part);
                st = fs_write(h, 1, part, &got);
                if (st && st != E_NOSPC) { fprintf(stderr, "write: %d\n", st); fails++; break; }
                memcpy(model[k] + mlen[k], tmp + done, got);
                mlen[k] += got;
                done += got;
                if (st) break;
            }
            fs_close(h);
            free(tmp);
            break;
        }
        case 2: {                                   /* read all and compare */
            long pos = 0;
            if (!exists[k]) {
                st = fs_open(names[k], FO_READ, 1, &h);
                if (st != E_NOENT) { fprintf(stderr, "open of a missing %s gave %d\n", names[k], st); fails++; }
                break;
            }
            st = fs_open(names[k], FO_READ, 1, &h);
            if (st) { fprintf(stderr, "reopen %s: %d\n", names[k], st); fails++; break; }
            for (;;) {
                u16 want = 1 + rnd() % 3000;
                st = fs_read(h, 1, want, &got);
                if (st) { fprintf(stderr, "read: %d\n", st); fails++; break; }
                if (!got) break;
                if (pos + got > mlen[k] || memcmp(cbuf, model[k] + pos, got)) { fprintf(stderr, "content differs: %s at %ld\n", names[k], pos); fails++; break; }
                pos += got;
            }
            if (pos != mlen[k]) { fprintf(stderr, "length of %s: %ld, expected %ld\n", names[k], pos, mlen[k]); fails++; }
            fs_close(h);
            break;
        }
        case 3: {                                   /* seek and read */
            u16 hi, lo;
            long p;
            if (!exists[k] || !mlen[k]) break;
            st = fs_open(names[k], FO_READ, 1, &h);
            if (st) { fails++; break; }
            p = rnd() % mlen[k];
            st = fs_seek(h, p >> 16, p & 0xFFFF, 0, &hi, &lo);
            if (st || ((long)hi << 16 | lo) != p) { fprintf(stderr, "seek %ld: %d %ld\n", p, st, (long)hi << 16 | lo); fails++; }
            st = fs_read(h, 1, 700, &got);
            if (st || memcmp(cbuf, model[k] + p, got) || (got < 700 && p + got != mlen[k])) { fprintf(stderr, "read after seek %s %ld\n", names[k], p); fails++; }
            /* seek from the end and from here (negative) */
            st = fs_seek(h, 0xFFFF, (u16)-100, 2, &hi, &lo);
            if (mlen[k] >= 100 && (st || ((long)hi << 16 | lo) != mlen[k] - 100)) { fprintf(stderr, "seek -100 from end\n"); fails++; }
            fs_close(h);
            break;
        }
        case 4:                                     /* delete */
            if (!exists[k]) break;
            st = fs_remove(names[k]);
            if (st) { fprintf(stderr, "remove %s: %d\n", names[k], st); fails++; }
            exists[k] = 0;
            mlen[k] = 0;
            break;
        case 5: {                                   /* rename within the volume */
            int j = rnd() % NF;
            if (!exists[k] || exists[j]) break;
            st = fs_rename(names[k], names[j]);
            if (st) { fprintf(stderr, "rename %s %s: %d\n", names[k], names[j], st); fails++; break; }
            memcpy(model[j], model[k], mlen[k]);
            mlen[j] = mlen[k];
            exists[j] = 1;
            exists[k] = 0;
            break;
        }
        }
    }
    fs_sync();
    printf("random test: %d operations, %d failures, %ld block reads, %ld block writes\n", count, fails, nreads, nwrites);
    return fails != 0;
}

int main(int argc, char **argv)
{
    u8 st;
    int rc = 0;
    if (argc < 3) { fprintf(stderr, "usage: fs_host IMAGE op ...\n"); return 2; }
    img = fopen(argv[1], "r+b");
    if (!img) { perror(argv[1]); return 2; }
    st = fs_mount();
    if (st) return die("mount", st);
    if (!strcmp(argv[2], "ls")) rc = do_ls(argv[3]);
    else if (!strcmp(argv[2], "cat")) rc = do_cat(argv[3], stdout);
    else if (!strcmp(argv[2], "put")) rc = do_put(argv[3], argv[4]);
    else if (!strcmp(argv[2], "rm")) rc = (st = fs_remove(argv[3])) ? die("rm", st) : 0;
    else if (!strcmp(argv[2], "mkdir")) rc = (st = fs_mkdir(argv[3])) ? die("mkdir", st) : 0;
    else if (!strcmp(argv[2], "mv")) rc = (st = fs_rename(argv[3], argv[4])) ? die("mv", st) : 0;
    else if (!strcmp(argv[2], "random")) rc = random_test(atoi(argv[3]), atoi(argv[4]));
    else { fprintf(stderr, "unknown operation\n"); rc = 2; }
    fs_sync();
    fclose(img);
    return rc;
}
