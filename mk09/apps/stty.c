/* SPDX-License-Identifier: MIT */
/*
 * stty - show or set the console mode
 *
 *   stty                      show
 *   stty utf8|euc|sjis|ascii  character set of the terminal (for line editing and for jwp)
 *   stty raw|cooked|echo|-echo
 *   stty cols=N rows=N        screen size reported to programs
 *   stty hdmi                 tell the HDMI screen of the FPGA board the character set (ESC % G, E or S)
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

int main(int argc, char **argv)
{
    u8 mode, cs, cols, rows;
    int i;
    char *a;
    u8 hdmi = 0;

    tty_getmode(&mode, &cs, &cols, &rows);
    for (i = 1; i < argc; i++) {
        a = argv[i];
        if (!strcmp(a, "raw")) mode = (mode | TM_RAW) & ~TM_ECHO;
        else if (!strcmp(a, "cooked")) mode = TM_DEFAULT;
        else if (!strcmp(a, "echo")) mode |= TM_ECHO;
        else if (!strcmp(a, "-echo")) mode &= ~TM_ECHO;
        else if (!strcmp(a, "utf8")) cs = CS_UTF8;
        else if (!strcmp(a, "euc")) cs = CS_EUC;
        else if (!strcmp(a, "sjis")) cs = CS_SJIS;
        else if (!strcmp(a, "ascii")) cs = CS_RAW;
        else if (!strcmp(a, "hdmi")) hdmi = 1;
        else if (!strncmp(a, "cols=", 5)) cols = (u8)atoi_(a + 5);
        else if (!strncmp(a, "rows=", 5)) rows = (u8)atoi_(a + 5);
        else { printf("stty: %s?\n", a); return 1; }
    }
    tty_setmode(mode, cs, cols, rows);
    if (hdmi) {
        static char esc[4] = { 27, '%', 'G', 0 };
        esc[2] = cs == CS_EUC ? 'E' : cs == CS_SJIS ? 'S' : cs == CS_UTF8 ? 'G' : '@';
        write(1, esc, 3);
    }
    if (argc == 1)
        printf("%s %s %dx%d%s\n", (cs == CS_UTF8 ? "utf8" : cs == CS_EUC ? "euc" : cs == CS_SJIS ? "sjis" : "ascii"), (mode & TM_RAW) ? "raw" : "cooked", cols, rows,
               (mode & TM_ECHO) ? " echo" : "");
    return 0;
}
