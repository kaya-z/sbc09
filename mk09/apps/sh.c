/* SPDX-License-Identifier: MIT */
/*
 * sh - the MK09 command interpreter
 *
 * Reads a line, splits it into words (quotes group; > file, >> file and < file redirect)
 * and either does a built-in command (cd pwd exit sync source shutdown ver help) or asks PM
 * to run the program NAME.MX, looked for in the current directory and in /BIN.
 * The shell sits at the top of the transient program area; PM takes it out of memory
 * when a big program needs the space and starts it again afterwards (sh -r).
 *
 * At the start the file /ETC/RC.SH is read as a script if it exists.
 * (The shell is kept small: it has to share the memory with the programs.)
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define LINEMAX     100
#define MAXARG      9

static char blk[ARGMAX];
static char path[MAXPATH + 8];
static char cwd[MAXPATH];
static u8 depth;

static const char builtins[] = "cd\0pwd\0exit\0sync\0source\0.\0shutdown\0halt\0ver\0help\0";

static void err(const char *what, int e)
{
    char n[6];
    putstr(what);
    if (e == -E_NOENT) {
        puts(": not found");
        return;
    }
    putstr(": error ");
    utoa_((u16)-e, n, 10);
    puts(n);
}

/* split the line in place: words in args[], redirections in the output parameters */
static int parse(char *s, char **args, char **in, char **out, u8 *append)
{
    int n = 0;
    char *w;
    char c, q;
    *in = *out = NULL;
    *append = 0;
    for (;;) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '\n') break;
        if (*s == '>' || *s == '<') {
            c = *s++;
            if (c == '>' && *s == '>') { *append = 1; s++; }
            while (*s == ' ' || *s == '\t') s++;
            w = s;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '<' && *s != '>') s++;
            if (*s) *s++ = 0;
            if (!*w) return -1;
            if (c == '>') *out = w; else *in = w;
            continue;
        }
        if (n >= MAXARG) return -1;
        w = s;
        args[n++] = w;
        q = 0;
        while (*s && *s != '\n') {                  /* the word, with quotes taken out */
            if (!q && (*s == ' ' || *s == '\t' || *s == '>' || *s == '<')) break;
            if (!q && (*s == '"' || *s == '\'')) { q = *s++; continue; }
            if (q && *s == q) { q = 0; s++; continue; }
            *w++ = *s++;
        }
        if (*s) s++;
        *w = 0;
    }
    return n;
}

static int exists(const char *p)
{
    mkstat_t st;
    return stat(p, &st) == 0 && !(st.attr & FA_DIR);
}

/* the full name of the program for a command word */
static int find_program(const char *name)
{
    if (strlen(name) > MAXPATH - 8) return 0;
    strcpy(path, name);
    if (strchr(name, '/') && exists(path)) return 1;
    strcat(path, ".MX");
    if (exists(path) || strchr(name, '/')) return exists(path);
    strcpy(path, "/BIN/");
    strcat(path, name);
    strcat(path, ".MX");
    return exists(path);
}

static int run(int n, char **args, char *in, char *out, u8 append)
{
    int fin = -1, fout = -1, i, r;
    u16 len, hin = NOHANDLE, hout = NOHANDLE;
    u8 l;

    if (!find_program(args[0])) {
        putstr(args[0]);
        puts(": command not found");
        return 127;
    }
    strcpy(blk, path);
    len = (u16)strlen(path) + 1;
    for (i = 1; i < n; i++) {
        l = (u8)strlen(args[i]) + 1;
        if (len + l + 1 > ARGMAX - SB_ARGS) { puts("sh: arguments too long"); return 1; }
        memcpy(blk + len, args[i], l);
        len += l;
    }
    blk[len++] = 0;
    if (in) {
        fin = open(in, FO_READ);
        if (fin < 0) { err(in, fin); return 1; }
        hin = fin - 3;
    }
    if (out) {
        fout = open(out, FO_WRITE | FO_CREATE | (append ? FO_APPEND : FO_TRUNC));
        if (fout < 0) { err(out, fout); if (fin >= 0) close(fin); return 1; }
        hout = fout - 3;
    }
    r = spawn(blk, len, hin, hout);
    if (r < 0) {
        err(args[0], r);
        if (fin >= 0) close(fin);                   /* (PM closes the files of a program that ran) */
        if (fout >= 0) close(fout);
        return 126;
    }
    return r;
}

static int execute(char *s);

static int do_source(const char *file)
{
    int fd = open(file, FO_READ), r, i, k = 0;
    char buf[64];
    char ln[LINEMAX];
    u8 c;

    if (fd < 0) return fd;
    if (depth >= 2) { close(fd); return -E_INVAL; }
    depth++;
    for (;;) {
        r = read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        for (i = 0; i < r; i++) {
            c = (u8)buf[i];
            if (c == '\n' || c == '\r') {
                if (k) { ln[k] = 0; k = 0; execute(ln); }
            } else if (k < LINEMAX - 2) {
                ln[k++] = c;
            }
        }
    }
    if (k) { ln[k] = 0; execute(ln); }
    close(fd);
    depth--;
    return 0;
}

static int execute(char *s)
{
    char *args[MAXARG + 1];
    char *in, *out;
    const char *b;
    u8 append, id = 0;
    int n, r = 0;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == 0) return 0;
    n = parse(s, args, &in, &out, &append);
    if (n < 0) { puts("sh: syntax error"); return 1; }
    if (n == 0) return 0;
    for (b = builtins; *b; b += strlen(b) + 1) {
        id++;
        if (!strcmp(args[0], b)) break;
    }
    if (!*b) id = 0;
    switch (id) {
    case 1:                                         /* cd */
        r = chdir(n > 1 ? args[1] : "/");
        if (r < 0) err(n > 1 ? args[1] : "/", r);
        return r < 0;
    case 2:                                         /* pwd */
        getcwd(cwd, sizeof(cwd));
        puts(cwd);
        return 0;
    case 3:                                         /* exit */
        exit(n > 1 ? atoi_(args[1]) : 0);
        return 0;
    case 4:
        sync();
        return 0;
    case 5:                                         /* source */
    case 6:
        if (n < 2) { puts("usage: source file"); return 1; }
        r = do_source(args[1]);
        if (r < 0) err(args[1], r);
        return r < 0;
    case 7:                                         /* shutdown */
    case 8:
        shutdown();
        return 0;
    case 9:
        puts("MK09 microkernel 1.0 (MC6809)");
        return 0;
    case 10:
        puts("cd [dir]  pwd  source file  sync  shutdown  exit  ver\n"
             "other commands are programs: ls cat cp mv rm mkdir date ps df stty jwp ... (/BIN)\n"
             "name > file   name >> file   name < file");
        return 0;
    }
    return run(n, args, in, out, append);
}

int main(int argc, char **argv)
{
    static char line[LINEMAX];
    int r;

    if (!(argc > 1 && !strcmp(argv[1], "-r"))) {
        puts("\n"
             " __  __ _  __  ___   ___\n"
             "|  \\/  | |/ / / _ \\ / _ \\\n"
             "| |\\/| | ' / | | | | (_) |\n"
             "| |  | | . \\ | |_| |\\__, |\n"
             "|_|  |_|_|\\_\\ \\___/   /_/\n"
             "\n"
             " MC6809 microkernel 1.0  --  64KB flat, no MMU\n"
             " TTY + DISK + FS(FAT16) + PM  /  type help\n");
        do_source("/ETC/RC.SH");
    }
    for (;;) {
        getcwd(cwd, sizeof(cwd));
        putstr(cwd);
        putstr("> ");
        fflush();
        r = read(0, line, LINEMAX - 1);
        if (r < 0) break;
        if (r == 0) { putchar('\n'); continue; }
        line[r] = 0;
        execute(line);
    }
    return 0;
}
