/* SPDX-License-Identifier: MIT */
/*
 * fstest - a test of the file system server from the inside (it is not needed to use MK09)
 *
 *   fstest [seed [count]]
 *
 * 1. random operations on six files in /FSTEST (create, append, truncate, read, seek, delete,
 *    rename) compared with a model in memory;
 * 2. a directory with 60 entries (the directory grows over several clusters), listed and emptied;
 * 3. a file of 70000 bytes (positions beyond 64KB), written in pieces and read back, with seeks.
 * Prints "fstest: ok" or the first difference.  (The same checks with the portable C code run on the
 * host in tests/fs_host.c; this one runs the code as compiled for the 6809, in the real system.)
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define NF      6
#define MAXF    2600

static const char names[NF][18] = { "/FSTEST/A.DAT", "/FSTEST/B.DAT", "/FSTEST/D/C.DAT", "/FSTEST/D/E.DAT", "/FSTEST/D/S/F.DAT", "/FSTEST/G.DAT" };
static u8 model[NF][MAXF];
static u16 mlen[NF];
static u8 exists[NF];
static u8 buf[MAXF + 64];
static u16 seed;
static u8 bad;

static u16 rnd(void)
{
    seed = seed * 25173 + 13849;
    return seed;
}

static void fail(const char *what, int v)
{
    if (!bad) printf("fstest: %s (%d)\n", what, v);
    bad = 1;
}

static void random_ops(u16 count)
{
    u16 op, k, n, i, got, p, j;
    int fd, r;
    u8 trunc;
    u16 hi, lo;

    for (op = 0; op < count && !bad; op++) {
        k = rnd() % NF;
        switch (rnd() % 6) {
        case 0:
        case 1:                                         /* write: replace or append */
            trunc = (rnd() % 3 == 0) || !exists[k];
            n = 1 + rnd() % 1500;
            if (!trunc && mlen[k] + n > MAXF) n = MAXF - mlen[k];
            if (!n) break;
            for (i = 0; i < n; i++) buf[i] = (u8)rnd();
            fd = open(names[k], FO_WRITE | FO_CREATE | (trunc ? FO_TRUNC : FO_APPEND));
            if (fd < 0) { fail("open for write", fd); break; }
            if (trunc) mlen[k] = 0;
            exists[k] = 1;
            for (i = 0; i < n; i += got) {
                p = 1 + rnd() % 700;
                if (p > n - i) p = n - i;
                r = write(fd, buf + i, p);
                if (r != (int)p) { fail("write", r); break; }
                got = p;
            }
            memcpy(model[k] + mlen[k], buf, n);
            mlen[k] += n;
            close(fd);
            break;
        case 2:                                         /* read it all */
            fd = open(names[k], FO_READ);
            if (!exists[k]) {
                if (fd != -E_NOENT) fail("open of a missing file", fd);
                if (fd >= 0) close(fd);
                break;
            }
            if (fd < 0) { fail("open for read", fd); break; }
            for (p = 0;; p += r) {
                j = 1 + rnd() % 900;
                r = read(fd, buf, j);
                if (r < 0) { fail("read", r); break; }
                if (!r) break;
                if (p + r > mlen[k] || memcmp(buf, model[k] + p, r)) { fail("content differs", p); break; }
            }
            if (!bad && p != mlen[k]) fail("length", p);
            close(fd);
            break;
        case 3:                                         /* seek and read */
            if (!exists[k] || !mlen[k]) break;
            fd = open(names[k], FO_READ);
            if (fd < 0) { fail("open", fd); break; }
            p = rnd() % mlen[k];
            r = seek(fd, 0, p, 0, &hi, &lo);
            if (r < 0 || hi || lo != p) fail("seek", p);
            r = read(fd, buf, 300);
            if (r < 0 || (u16)r != (mlen[k] - p < 300 ? mlen[k] - p : 300) || memcmp(buf, model[k] + p, r)) fail("read after seek", p);
            if (mlen[k] > 40) {
                r = seek(fd, 0xFFFF, (u16)-40, 2, &hi, &lo);        /* 40 before the end */
                if (r < 0 || lo != mlen[k] - 40) fail("seek from the end", lo);
                r = read(fd, buf, 100);
                if (r != 40 || memcmp(buf, model[k] + mlen[k] - 40, 40)) fail("read at the end", r);
            }
            close(fd);
            break;
        case 4:
            if (!exists[k]) break;
            r = unlink(names[k]);
            if (r < 0) fail("unlink", r);
            exists[k] = 0;
            mlen[k] = 0;
            break;
        case 5:                                         /* rename onto a free name */
            j = rnd() % NF;
            if (!exists[k] || exists[j]) break;
            r = rename(names[k], names[j]);
            if (r < 0) { fail("rename", r); break; }
            memcpy(model[j], model[k], mlen[k]);
            mlen[j] = mlen[k];
            exists[j] = 1;
            exists[k] = 0;
            break;
        }
    }
}

static void many_files(void)
{
    char nm[24];
    dirent_t d;
    int fd, r, n, i;
    u8 seen[60];

    mkdir("/FSTEST/M");
    for (i = 0; i < 60; i++) {
        strcpy(nm, "/FSTEST/M/F00.TXT");
        nm[11] = (char)('0' + i / 10);
        nm[12] = (char)('0' + i % 10);
        fd = open(nm, FO_WRITE | FO_CREATE | FO_TRUNC);
        if (fd < 0) { fail("create in the big directory", i); return; }
        buf[0] = (u8)i;
        write(fd, buf, 1);
        close(fd);
    }
    memset(seen, 0, sizeof(seen));
    fd = open("/FSTEST/M", FO_DIR | FO_READ);
    if (fd < 0) { fail("open the big directory", fd); return; }
    n = 0;
    while ((r = readdir(fd, &d)) > 0) {
        if (d.name[0] == '.') continue;
        i = (d.name[1] - '0') * 10 + (d.name[2] - '0');
        if (d.name[0] != 'F' || i < 0 || i >= 60 || seen[i] || d.sizelo != 1) { fail("directory entry", n); break; }
        seen[i] = 1;
        n++;
    }
    close(fd);
    if (n != 60) fail("number of entries", n);
    for (i = 0; i < 60; i++) {
        strcpy(nm, "/FSTEST/M/F00.TXT");
        nm[11] = (char)('0' + i / 10);
        nm[12] = (char)('0' + i % 10);
        if (unlink(nm) < 0) { fail("unlink in the big directory", i); return; }
    }
    if (unlink("/FSTEST/M") < 0) fail("remove the big directory", 0);
}

static void big_file(void)
{
    u16 i, j, hi, lo;
    int fd, r;
    unsigned long total = 70000UL, pos;

    fd = open("/FSTEST/BIG.DAT", FO_WRITE | FO_CREATE | FO_TRUNC);
    if (fd < 0) { fail("create the big file", fd); return; }
    for (pos = 0; pos < total; pos += 1000) {
        for (i = 0; i < 1000; i++) buf[i] = (u8)((pos + i) * 7 + ((pos + i) >> 8));
        r = write(fd, buf, 1000);
        if (r != 1000) { fail("write the big file", r); close(fd); return; }
    }
    close(fd);
    fd = open("/FSTEST/BIG.DAT", FO_READ);
    if (fd < 0) { fail("open the big file", fd); return; }
    for (pos = 0; pos < total; pos += 700) {
        j = (total - pos < 700) ? (u16)(total - pos) : 700;
        r = read(fd, buf, j);
        if (r != (int)j) { fail("read the big file", r); break; }
        for (i = 0; i < j; i++)
            if (buf[i] != (u8)((pos + i) * 7 + ((pos + i) >> 8))) { fail("big file content", (int)pos + i); break; }
        if (bad) break;
    }
    if (!bad) {
        r = seek(fd, 1, 0x0834, 0, &hi, &lo);                  /* 65536 + 2100 */
        if (r < 0 || hi != 1 || lo != 0x0834) fail("seek beyond 64KB", lo);
        r = read(fd, buf, 50);
        pos = 0x10834UL;
        for (i = 0; i < 50 && r == 50; i++)
            if (buf[i] != (u8)((pos + i) * 7 + ((pos + i) >> 8))) { fail("content beyond 64KB", i); break; }
        r = seek(fd, 0, 0, 2, &hi, &lo);                       /* the end */
        if (r < 0 || hi != 1 || lo != (u16)(70000UL - 65536UL)) fail("seek to the end", lo);
    }
    close(fd);
    if (unlink("/FSTEST/BIG.DAT") < 0) fail("remove the big file", 0);
}

int main(int argc, char **argv)
{
    u16 count = 120;
    int i;

    seed = argc > 1 ? (u16)atoi_(argv[1]) : 1;
    if (argc > 2) count = (u16)atoi_(argv[2]);
    mkdir("/FSTEST");
    mkdir("/FSTEST/D");
    mkdir("/FSTEST/D/S");
    random_ops(count);
    for (i = 0; i < NF; i++)
        if (exists[i] && unlink(names[i]) < 0) fail("cleanup", i);
    if (!bad) many_files();
    if (!bad) big_file();
    unlink("/FSTEST/D/S");
    unlink("/FSTEST/D");
    if (unlink("/FSTEST") < 0 && !bad) fail("remove the test directory", 0);
    if (!bad) puts("fstest: ok");
    return bad;
}
