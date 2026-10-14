/* SPDX-License-Identifier: MIT */
/*
 * vdisk.c - Virtual RBF: a host directory presented to OS-9 as a disk
 *
 * Credits: derived from sbc09 src/vdisk.c, "Virtual RBF - Random Block File
 * Manager", Shinji KONO (kono@ie.u-ryukyu.ac.jp), 2018/7/17, GPL.  The
 * guest side protocol (the VRBF file manager in os9/levelN/vrbf.asm writes a
 * command byte $D1..$DD to the disk command port with the caller's stack
 * frame address, path descriptor and current directory number in the
 * neighbouring registers) is unchanged, so the unmodified VRBF module
 * works.  Changes with respect to the original:
 *   - data/path buffers are copied byte-wise through the MMU so that they
 *     may cross 8K block boundaries,
 *   - real OS-9 error numbers instead of a generic $FF,
 *   - Create fails with E$CEF if the file exists, Open for writing does not
 *     truncate, ReadLn returns the number of bytes actually read,
 *   - directories are returned sorted and 8-bit (Japanese) file names can be
 *     passed in either direction (see kanji_names below).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "machine.h"
#include "console.h"

#define MAXPDV 256
#define MAXVDRV 4
#define DIR_SZ 32
#define DIR_NM 29

/* OS-9 error codes */
#define E_EOF   0xd3   /* 211 end of file            */
#define E_FNA   0xd6   /* 214 file not accessible    */
#define E_BPNAM 0xd7   /* 215 bad path name          */
#define E_PNNF  0xd8   /* 216 path name not found    */
#define E_CEF   0xda   /* 218 file already exists    */
#define E_WRITE 0xf5   /* 245 write error            */
#define E_READ  0xf4   /* 244 read error             */
#define E_UNKSVC 0xd0  /* 208 unknown service        */
#define E_NOTRDY 0xf6  /* 246 not ready              */

typedef struct pathdesc {
    char *name;       /* host path of the file */
    FILE *fp;
    int   mode;
    int   inode;
    int   num;
    int   sz;         /* directory image size */
    char  drv;
    char  use;
    char  dir;
    char *dirfp;      /* simulated OS-9 directory file image */
} pathdesc_t;

static pathdesc_t pdv[MAXPDV];
static const char *drv_root[MAXVDRV] = { ".", ".", ".", "." };
static machine_t *vm;
static uint8_t pmmu[8];
int vdisk_debug;

/* cross check switches (tests/crosscheck): VDISK_UNSORTED=1 keeps readdir order, VDISK_COMPAT=1
 * makes the private GetStat codes fail like the reference implementation */
static int vdisk_env(const char *name)
{
    return getenv(name) != NULL;
}
int vdisk_kanji_names;       /* 1: bytes >= 0x80 are file name characters */

int vdisk_set_root(int drv, const char *dir)
{
    if (drv < 0 || drv >= MAXVDRV) return -1;
    drv_root[drv] = dir;
    return 0;
}

/* ------------------------------------------------------------------ */
/* guest memory access                                                 */
/* ------------------------------------------------------------------ */
static uint8_t *sysmap(void) { return vm->level2 ? vm->mem + 0xffa0 : NULL; }
static uint8_t *smem(uint16_t a) { return machine_ptr(vm, a, sysmap()); }
static uint8_t *pmem(uint16_t a) { return machine_ptr(vm, a, pmmu); }

static void setword(uint16_t a, uint16_t v) { *smem(a) = (uint8_t)(v >> 8); *smem((uint16_t)(a + 1)) = (uint8_t)v; }
static uint16_t sword(uint16_t a) { return (uint16_t)((*smem(a) << 8) | *smem((uint16_t)(a + 1))); }

static void from_guest(uint16_t addr, uint8_t *dst, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) dst[i] = *pmem((uint16_t)(addr + i));
}
static void to_guest(uint16_t addr, const uint8_t *src, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) *pmem((uint16_t)(addr + i)) = src[i];
}

/* process DAT image (Level 2: user buffers live in the caller's map) */
static void get_dat(void)
{
    if (!vm->level2) return;
    uint16_t ps = sword(0x50);
    for (int i = 0; i < 8; i++) pmmu[i] = *smem((uint16_t)(ps + 0x40 + i * 2 + 1));
}

/* ------------------------------------------------------------------ */
/* current directory table                                             */
/* ------------------------------------------------------------------ */
static char *cdt[256];
static int cdtptr;

static uint8_t setcd(const char *name)
{
    for (int i = 0; i < 256; i++)
        if (cdt[i] && !strcmp(name, cdt[i])) return (uint8_t)i;
    cdtptr &= 0xff;
    free(cdt[cdtptr]);
    cdt[cdtptr] = strdup(name);
    return (uint8_t)cdtptr++;
}

/* ------------------------------------------------------------------ */
/* path handling                                                       */
/* ------------------------------------------------------------------ */
static void closepd(pathdesc_t *pd)
{
    if (pd->fp) fclose(pd->fp);
    free(pd->dirfp);
    free(pd->name);
    pd->fp = NULL; pd->dirfp = NULL; pd->name = NULL;
    pd->dir = 0; pd->use = 0; pd->mode = 0;
}

/* an OS-9 pathlist ends at a control character / space, or (classic OS-9)
 * at the character with bit 7 set.  With kanji names, bytes >= 0x80 belong
 * to the name. */
static int name_char(uint8_t c)
{
    if (c > ' ' && c < 0x80) return 1;
    return vdisk_kanji_names && c >= 0x80;
}

static size_t fetch_path(uint16_t addr, char *buf, size_t max)
{
    size_t n = 0;
    while (n < max - 1) {
        uint8_t c = *pmem((uint16_t)(addr + n));
        buf[n++] = (char)c;
        if (!name_char(c)) {
            if (c == ' ')                       /* also fetch what follows the blanks */
                while (n < max - 1 && (buf[n] = (char)*pmem((uint16_t)(addr + n))) == ' ') n++;
            if (n < max - 1 && buf[n - 1] == ' ') { buf[n] = (char)*pmem((uint16_t)(addr + n)); n++; }
            break;
        }
    }
    buf[n] = 0;
    return n;
}

/* OS-9 names are case insensitive, host names are not: replace every path
 * component that does not exist as spelled by an existing entry that
 * differs only in case (same length for ASCII, so the string is fixed in
 * place).  Components that do not exist at all are left alone (create). */
static void ci_fix(char *path)
{
    char *p = path;
    if (*p == '/') p++;
    while (*p) {
        char *end = strchr(p, '/');
        char save;
        char dirbuf[1024];
        size_t dl = (size_t)(p - path);
        DIR *d;
        struct dirent *de;
        if (end) { save = *end; *end = 0; } else save = 0;
        if (*p && access(path, F_OK) != 0) {
            if (dl == 0) strcpy(dirbuf, ".");
            else if (dl == 1) strcpy(dirbuf, "/");        /* component directly below the root */
            else { memcpy(dirbuf, path, dl - 1); dirbuf[dl - 1] = 0; }   /* strip the '/' */
            d = opendir(dirbuf);
            if (d) {
                while ((de = readdir(d)) != NULL) {
                    if (strcasecmp(de->d_name, p) == 0 && strlen(de->d_name) == strlen(p)) {
                        memcpy(p, de->d_name, strlen(p));
                        break;
                    }
                }
                closedir(d);
            }
        }
        if (!end) break;
        *end = save;
        p = end + 1;
    }
}

/* Build the host path for the guest path in `path`.  Returns pointer to
 * the first character after the path (or NULL).  *host is malloc'ed. */
static char *host_path(char *path, int drv, int curdir, char **host)
{
    char *p = path;
    char name[512], hname[1600];
    size_t n = 0;
    const char *base;
    char *res;
    int absolute = (path[0] == '/');
    int mode = console_kanji_mode();

    while (name_char((uint8_t)*p) && n < sizeof name - 2) { name[n++] = *p++; }
    if (*p && (*p & 0x80) && !vdisk_kanji_names) { name[n++] = (char)(*p & 0x7f); p++; }
    name[n] = 0;
    while (*p == ' ') p++;
    if (n == 0) return NULL;

    if (vdisk_kanji_names && mode != KANJI_RAW) {
        size_t k = kanji_guest_to_host(mode, (uint8_t *)name, n, hname, sizeof hname - 1);
        hname[k] = 0;
    } else {
        memcpy(hname, name, n + 1);
    }

    if (absolute) {
        char *q = hname + 1;
        while (*q && *q != '/') q++;             /* skip the /device part */
        base = drv_root[drv & 3];
        res = malloc(strlen(base) + strlen(q) + 2);
        strcpy(res, base);
        if (*q) strcat(res, q);
    } else {
        base = cdt[curdir] ? cdt[curdir] : drv_root[drv & 3];
        res = malloc(strlen(base) + strlen(hname) + 2);
        sprintf(res, "%s/%s", base, hname);
    }
    ci_fix(res);
    *host = res;
    return p;
}

/* guest (EUC/SJIS) name -> host (UTF-8) name, in place */
static void name_to_host(char *s, size_t cap)
{
    int mode = console_kanji_mode();
    char tmp[1024];
    size_t n;
    if (!vdisk_kanji_names || mode == KANJI_RAW) return;
    n = kanji_guest_to_host(mode, (uint8_t *)s, strlen(s), tmp, sizeof tmp - 1);
    if (n >= cap) n = cap - 1;
    memcpy(s, tmp, n);
    s[n] = 0;
}

static int map_errno(int e)
{
    switch (e) {
    case ENOENT: case ENOTDIR: return E_PNNF;
    case EACCES: case EPERM: case EISDIR: case EROFS: return E_FNA;
    case EEXIST: return E_CEF;
    default: return 0xff;
    }
}

/* ------------------------------------------------------------------ */
/* OS-9 attributes / dates / FD sector                                 */
/* ------------------------------------------------------------------ */
static uint8_t os9attr(mode_t mode)
{
    uint8_t m = 0;
    if (S_ISDIR(mode)) m |= 0x80;
    if (mode & S_IRUSR) m |= 0x01;
    if (mode & S_IWUSR) m |= 0x02;
    if (mode & S_IXUSR) m |= 0x04;
    if (mode & S_IROTH) m |= 0x08;
    if (mode & S_IWOTH) m |= 0x10;
    if (mode & S_IXOTH) m |= 0x20;
    return m | 0x60;
}

static mode_t unixmode(uint8_t a)
{
    mode_t m = 0;
    if (a & 0x01) m |= S_IRUSR | S_IRGRP;
    if (a & 0x02) m |= S_IWUSR;
    if (a & 0x04) m |= S_IXUSR | S_IXGRP;
    if (a & 0x08) m |= S_IROTH;
    if (a & 0x10) m |= S_IWOTH;
    if (a & 0x20) m |= S_IXOTH;
    if (!(m & S_IRUSR)) m |= S_IRUSR;       /* never create unreadable files */
    return m;
}

static void os9date(uint8_t *d, time_t t)
{
    struct tm r;
    localtime_r(&t, &r);
    d[0] = (uint8_t)r.tm_year;
    d[1] = (uint8_t)(r.tm_mon + 1);
    d[2] = (uint8_t)r.tm_mday;
    d[3] = (uint8_t)r.tm_hour;
    d[4] = (uint8_t)r.tm_min;
    d[5] = (uint8_t)r.tm_sec;
}

static int fd_sector(uint8_t *buf, const char *host)
{
    struct stat st;
    if (stat(host, &st) != 0) return E_PNNF;
    memset(buf, 0, 16);
    buf[0] = os9attr(st.st_mode);
    buf[1] = (uint8_t)(st.st_uid >> 8); buf[2] = (uint8_t)st.st_uid;
    os9date(buf + 3, st.st_mtime);
    buf[8] = (uint8_t)st.st_nlink;
    buf[9] = (uint8_t)(st.st_size >> 24); buf[10] = (uint8_t)(st.st_size >> 16);
    buf[11] = (uint8_t)(st.st_size >> 8); buf[12] = (uint8_t)st.st_size;
    os9date(buf + 13, st.st_ctime);
    return 0;
}

/* ------------------------------------------------------------------ */
/* directories                                                         */
/* ------------------------------------------------------------------ */
static int os9opendir(pathdesc_t *pd)
{
    struct dirent **list = NULL;
    int n, i;
    if (pd->dirfp) return 0;
    n = scandir(pd->name, &list, NULL, vdisk_env("VDISK_UNSORTED") ? NULL : alphasort);
    if (n < 0) return -1;
    pd->sz = n * DIR_SZ;
    pd->dirfp = calloc(1, (size_t)pd->sz + 1);
    for (i = 0; i < n; i++) {
        char *e = pd->dirfp + i * DIR_SZ;
        const char *nm = list[i]->d_name;
        size_t len = strlen(nm), j;
        uint8_t gname[DIR_NM + 1];
        size_t glen = len;
        int mode = console_kanji_mode();
        if (vdisk_kanji_names && mode != KANJI_RAW) {
            glen = kanji_host_to_guest(mode, nm, len, gname, DIR_NM);
        } else {
            memcpy(gname, nm, len > DIR_NM ? DIR_NM : len);
            if (glen > DIR_NM) glen = DIR_NM;
        }
        for (j = 0; j < glen && j < DIR_NM; j++) e[j] = (char)gname[j];
        if (vdisk_kanji_names) {
            /* 8-bit names: no bit 7 end marker (name is NUL padded) */
        } else {
            for (j = 0; j < glen && j < DIR_NM; j++) e[j] &= 0x7f;
            if (glen) e[glen > DIR_NM ? DIR_NM - 1 : glen - 1] |= (char)0x80;
        }
        {
            unsigned ino = (unsigned)list[i]->d_ino;
            e[DIR_NM] = (char)((ino >> 16) & 0xff);
            e[DIR_NM + 1] = (char)((ino >> 8) & 0xff);
            e[DIR_NM + 2] = (char)(ino & 0xff);
        }
        free(list[i]);
    }
    free(list);
    pd->fp = fmemopen(pd->dirfp, (size_t)pd->sz + 1, "r");
    return 0;
}

/* SS.FDInf: return the file descriptor of any entry of an open directory */
static int fdinfo(uint8_t *buf, int len, int inode)
{
    (void)len;
    for (int i = 0; i < MAXPDV; i++) {
        pathdesc_t *pd = &pdv[i];
        if (!pd->use || !pd->dir || !pd->dirfp) continue;
        for (int off = 0; off < pd->sz; off += DIR_SZ) {
            uint8_t *p = (uint8_t *)pd->dirfp + off + DIR_NM;
            int dino = (p[0] << 16) | (p[1] << 8) | p[2];
            if (dino == inode) {
                char nm[DIR_NM * 3 + 8];
                char *host;
                int k;
                char path[600];
                memcpy(nm, pd->dirfp + off, DIR_NM);
                nm[DIR_NM] = 0;
                for (k = 0; k < DIR_NM; k++) {
                    if (!vdisk_kanji_names && (nm[k] & 0x80)) { nm[k] &= 0x7f; nm[k + 1] = 0; break; }
                }
                name_to_host(nm, sizeof nm);
                snprintf(path, sizeof path, "%s/%s", pd->name, nm);
                host = path;
                return fd_sector(buf, host);
            }
        }
    }
    return 0xff;
}

/* ------------------------------------------------------------------ */
/* the command dispatcher                                              */
/* ------------------------------------------------------------------ */
static const char *fmode(uint8_t access)
{
    return (access & 2) ? "r+" : "r";
}

void do_vdisk(machine_t *m, uint8_t cmd)
{
    uint8_t *io = m->level2 ? m->mem + 0xfe00 : m->mem1 + 0xe000;
    uint8_t *r = io + (m->iopage & 0x1ff);
    int curdir = r[0x44];
    uint16_t u = (uint16_t)((r[0x45] << 8) | r[0x46]);
    uint16_t xreg, yreg, ureg;
    uint8_t areg, breg;
    uint8_t *os9pd;
    pathdesc_t *pd;
    char path[600];
    char *host = NULL, *next;
    size_t plen;

    vm = m;
    xreg = sword((uint16_t)(u + 4));
    yreg = sword((uint16_t)(u + 6));
    ureg = sword((uint16_t)(u + 8));
    areg = *smem((uint16_t)(u + 1));
    breg = *smem((uint16_t)(u + 2));
    /* the path descriptor pointer is a 16 bit value in r[0x47..0x48] */
    os9pd = smem((uint16_t)((r[0x47] << 8) | r[0x48]));
    pd = &pdv[*os9pd];
    get_dat();
    pd->num = *os9pd;
    pd->drv = (char)r[0x41];
    (void)ureg;

    switch (cmd) {
    case 0xd1: {                                  /* I$Create */
        int fd;
        int attr = breg;
        plen = fetch_path(xreg, path, sizeof path);
        (void)plen;
        pd->fp = NULL;
        free(pd->name); pd->name = NULL;
        next = host_path(path, pd->drv, curdir, &host);
        breg = 0xff;
        if (!next) { breg = E_BPNAM; break; }
        pd->name = host;
        fd = open(host, O_RDWR | O_CREAT | O_EXCL, unixmode((uint8_t)attr));
        if (fd < 0) { breg = (uint8_t)map_errno(errno); pd->use = 0; break; }
        pd->fp = fdopen(fd, "r+");
        if (!pd->fp) { close(fd); pd->use = 0; break; }
        breg = 0;
        areg = (uint8_t)pd->num;
        *smem((uint16_t)(u + 1)) = areg;
        xreg = (uint16_t)(xreg + (next - path));
        pd->use = 1;
        pd->dir = 0;
        break;
    }
    case 0xd2: {                                  /* I$Open */
        struct stat st;
        pd->fp = NULL;
        free(pd->name); pd->name = NULL;
        fetch_path(xreg, path, sizeof path);
        next = host_path(path, pd->drv, curdir, &host);
        breg = 0xff;
        if (!next) { breg = E_BPNAM; pd->use = 0; break; }
        pd->name = host;
        if (stat(host, &st) != 0) { breg = (uint8_t)map_errno(errno); pd->use = 0; break; }
        if (S_ISDIR(st.st_mode)) {
            pd->dir = 1;
            if (os9opendir(pd) != 0) { breg = E_FNA; pd->use = 0; break; }
        } else {
            pd->dir = 0;
            pd->fp = fopen(host, fmode(areg));
            if (!pd->fp && (areg & 2)) pd->fp = fopen(host, "r");
            if (!pd->fp) { breg = (uint8_t)map_errno(errno); pd->use = 0; break; }
        }
        if (pd->fp) {
            breg = 0;
            areg = (uint8_t)pd->num;
            *smem((uint16_t)(u + 1)) = areg;
            xreg = (uint16_t)(xreg + (next - path));
            pd->use = 1;
        } else {
            pd->use = 0;
        }
        break;
    }
    case 0xd3: {                                  /* I$MakDir */
        fetch_path(xreg, path, sizeof path);
        next = host_path(path, pd->drv, curdir, &host);
        breg = 0xff;
        if (!next) { breg = E_BPNAM; break; }
        if (mkdir(host, 0755) == 0) { xreg = (uint16_t)(xreg + (next - path)); breg = 0; }
        else breg = (uint8_t)map_errno(errno);
        free(host);
        break;
    }
    case 0xd4: {                                  /* I$ChgDir */
        struct stat st;
        fetch_path(xreg, path, sizeof path);
        next = host_path(path, pd->drv, curdir, &host);
        breg = 0xff;
        if (!next) { breg = E_BPNAM; break; }
        if (stat(host, &st) != 0) { breg = (uint8_t)map_errno(errno); free(host); break; }
        if (!S_ISDIR(st.st_mode)) { breg = E_PNNF; free(host); break; }
        xreg = (uint16_t)(xreg + (next - path));
        areg = setcd(host);
        *smem((uint16_t)(u + 1)) = areg;
        breg = 0;
        free(host);
        break;
    }
    case 0xd5: {                                  /* I$Delete */
        struct stat st;
        fetch_path(xreg, path, sizeof path);
        next = host_path(path, pd->drv, curdir, &host);
        breg = 0xff;
        pd->use = 0;
        if (!next) { breg = E_BPNAM; break; }
        if (stat(host, &st) != 0) { breg = (uint8_t)map_errno(errno); free(host); break; }
        if ((S_ISDIR(st.st_mode) ? rmdir(host) : unlink(host)) == 0) {
            xreg = (uint16_t)(xreg + (next - path));
            breg = 0;
        } else breg = (uint8_t)map_errno(errno);
        free(host);
        break;
    }
    case 0xd6: {                                  /* I$Seek */
        long pos = ((long)xreg << 16) | sword((uint16_t)(u + 8));
        breg = 0xff;
        if (pd->fp && fseek(pd->fp, pos, SEEK_SET) == 0) breg = 0;
        break;
    }
    case 0xd7: {                                  /* I$ReadLn */
        uint8_t *tmp;
        size_t n = 0;
        breg = 0xff;
        if (!pd->fp) break;
        tmp = malloc(yreg ? yreg : 1);
        while (n < yreg) {
            int ch = fgetc(pd->fp);
            if (ch == EOF) break;
            if (ch == '\n') ch = '\r';
            tmp[n++] = (uint8_t)ch;
            if (ch == '\r') break;
        }
        if (n == 0) breg = E_EOF;
        else { to_guest(xreg, tmp, n); breg = 0; }
        free(tmp);
        setword((uint16_t)(u + 6), (uint16_t)n);
        break;
    }
    case 0xd8: {                                  /* I$Read */
        uint8_t *tmp;
        size_t n;
        breg = 0xff;
        if (!pd->fp) break;
        tmp = malloc(yreg ? yreg : 1);
        n = fread(tmp, 1, yreg, pd->fp);
        if (n) to_guest(xreg, tmp, n);
        free(tmp);
        setword((uint16_t)(u + 6), (uint16_t)n);
        breg = n == 0 && yreg ? E_EOF : 0;
        break;
    }
    case 0xd9: {                                  /* I$WritLn */
        uint8_t *tmp;
        size_t n = 0, i;
        breg = 0xff;
        if (!pd->fp || pd->dir) { breg = E_WRITE; break; }
        tmp = malloc(yreg ? yreg : 1);
        from_guest(xreg, tmp, yreg);
        for (i = 0; i < yreg; i++) {
            if (tmp[i] == '\r') { fputc('\n', pd->fp); n = i + 1; break; }
            fputc(tmp[i], pd->fp);
            n = i + 1;
        }
        fflush(pd->fp);
        free(tmp);
        breg = 0;
        setword((uint16_t)(u + 6), (uint16_t)n);
        break;
    }
    case 0xda: {                                  /* I$Write */
        uint8_t *tmp;
        size_t n;
        breg = 0xff;
        if (!pd->fp || pd->dir) { breg = E_WRITE; break; }
        tmp = malloc(yreg ? yreg : 1);
        from_guest(xreg, tmp, yreg);
        n = fwrite(tmp, 1, yreg, pd->fp);
        fflush(pd->fp);
        free(tmp);
        breg = n == yreg ? 0 : E_WRITE;
        setword((uint16_t)(u + 6), (uint16_t)n);
        break;
    }
    case 0xdb:                                    /* I$Close */
        closepd(pd);
        breg = 0;
        break;
    case 0xdc: {                                  /* I$GetStat */
        struct stat st;
        long pos;
        if (vdisk_env("VDISK_COMPAT") && (breg == 0x00 || breg == 0xb0)) { breg = 0xff; break; }   /* cross check runs */
        switch (breg) {
        case 0x00: {                              /* SS.Opt: path options, RBF layout */
            /* The option section (PD.OPT, 32 bytes at offset $20 of the path
             * descriptor) is returned as it is; PD.ATT (offset 19) is filled with
             * the attributes of the file so that deldir, attr etc. can tell
             * directories from files. */
            uint16_t pdaddr = (uint16_t)((r[0x47] << 8) | r[0x48]);
            uint8_t opt[32];
            struct stat st2;
            for (int i = 0; i < 32; i++) opt[i] = *smem((uint16_t)(pdaddr + 32 + i));
            if (pd->name && stat(pd->name, &st2) == 0) opt[19] = os9attr(st2.st_mode);
            to_guest(xreg, opt, sizeof opt);
            breg = 0;
            break;
        }
        case 0x01:                                /* SS.Ready */
            breg = 0xff;
            if (!pd->fp) break;
            fstat(fileno(pd->fp), &st);
            pos = ftell(pd->fp);
            xreg = (uint16_t)(st.st_size - pos);
            breg = pos < st.st_size ? 0 : E_NOTRDY;
            break;
        case 0x02:                                /* SS.Size */
            breg = 0xff;
            if (!pd->fp) break;
            if (pd->dir) { xreg = 0; setword((uint16_t)(u + 8), (uint16_t)pd->sz); }
            else {
                fstat(fileno(pd->fp), &st);
                xreg = (uint16_t)(st.st_size >> 16);
                setword((uint16_t)(u + 8), (uint16_t)st.st_size);
            }
            breg = 0;
            break;
        case 0x05:                                /* SS.Pos */
            breg = 0xff;
            if (!pd->fp) break;
            pos = ftell(pd->fp);
            xreg = (uint16_t)(pos >> 16);
            setword((uint16_t)(u + 8), (uint16_t)pos);
            breg = 0;
            break;
        case 0x06:                                /* SS.EOF */
            breg = 0;
            break;
        case 0x0f: {                              /* SS.FD */
            uint8_t fd[256];
            int len = yreg > 256 ? 256 : yreg;
            breg = 0xff;
            if (!pd->fp || !pd->name) break;
            memset(fd, 0, sizeof fd);
            breg = (uint8_t)fd_sector(fd, pd->name);
            if (!breg) to_guest(xreg, fd, (size_t)len);
            break;
        }
        case 0x20: {                              /* SS.FDInf */
            uint8_t fd[256];
            int len = yreg & 0xff;
            breg = 0xff;
            if (!pd->fp) break;
            memset(fd, 0, sizeof fd);
            breg = (uint8_t)fdinfo(fd, len, ((yreg & 0xff00) >> 8) * 0x10000 + sword((uint16_t)(u + 8)));
            if (!breg) to_guest(xreg, fd, (size_t)(len ? len : 256));
            break;
        }
        case 0xb0:                                /* private SS.KNm: are directory names 8 bit? */
            xreg = vdisk_kanji_names ? 1 : 0;
            breg = 0;
            break;
        default:
            breg = 0xff;
        }
        break;
    }
    case 0xdd:                                    /* I$SetStat */
        switch (breg) {
        case 0x02:                                /* SS.Size: truncate/extend */
            breg = 0xff;
            if (pd->fp && !pd->dir) {
                long sz = ((long)xreg << 16) | sword((uint16_t)(u + 8));
                fflush(pd->fp);
                if (ftruncate(fileno(pd->fp), sz) == 0) breg = 0;
            }
            break;
        default:
            breg = 0xff;
        }
        break;
    default:
        breg = E_UNKSVC;
    }

    if (vdisk_debug && breg)
        fprintf(stderr, "vdisk: cmd %02x (function code %02x) failed with %d\n", cmd,
                *smem((uint16_t)(u + 2)) == breg ? 0 : 0, breg);
    r[0x40] = breg;
    *smem((uint16_t)(u + 2)) = breg;
    setword((uint16_t)(u + 4), xreg);
    (void)yreg;
}
