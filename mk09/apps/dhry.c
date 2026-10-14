/* SPDX-License-Identifier: MIT AND LicenseRef-Weicker-Dhrystone (see LICENSES.md, 4.2) */
/*
 * dhry - the Dhrystone 2.1 benchmark for MK09 (built with CMOC like the other programs).
 *
 *   dhry [-t seconds] | [runs]
 *
 * Without arguments the loop runs for at least 10 seconds; with a number it runs that many passes
 * and still prints the result.  The time is the uptime of the kernel (50 Hz ticks of 20 ms), so the
 * figure is that of the machine the kernel runs on (the simulated 1.8432 MHz, or the board).
 *
 * The procedures are the ones of os9/dhry/dhry.c (Dhrystone 2.1 by Reinhold P. Weicker; see the
 * notes there and in LICENSES.md, 4.2: single file, no separate compilation, so the "ground
 * rules" of the author do not hold).  Only the timing and the output are new.  The tick counter
 * of the kernel is 16 bits wide (it wraps after 21 minutes); the program adds up the differences.
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#define REG
#define Null 0
#define true  1
#define false 0

typedef enum { Ident_1, Ident_2, Ident_3, Ident_4, Ident_5 } Enumeration;

typedef int One_Thirty;
typedef int One_Fifty;
typedef char Capital_Letter;
typedef int Boolean;
typedef char Str_30[31];
typedef int Arr_1_Dim[50];
typedef int Arr_2_Dim[50][50];

struct var1 { Enumeration Enum_Comp; int Int_Comp; char Str_Comp[31]; };
struct var2 { Enumeration E_Comp_2; char Str_2_Comp[31]; };
struct var3 { char Ch_1_Comp; char Ch_2_Comp; };
union variant_u { struct var1 var_1; struct var2 var_2; struct var3 var_3; };

typedef struct record {
    struct record *Ptr_Comp;
    Enumeration Discr;
    union variant_u variant;
} Rec_Type, *Rec_Pointer;

/* globals of the Dhrystone program */
static Rec_Type Rec_Glob, Rec_Next;
Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
int Int_Glob;
Boolean Bool_Glob;
char Ch_1_Glob, Ch_2_Glob;
int Arr_1_Glob[50];
int Arr_2_Glob[50][50];

/* the last values of the locals of the loop (printed by the final check) */
One_Fifty Last_Int_1_Loc, Last_Int_2_Loc, Last_Int_3_Loc;
Enumeration Last_Enum_Loc;
Str_30 Str_1_Loc, Last_Str_2_Loc;

void Proc_1(Rec_Pointer Ptr_Val_Par);
void Proc_2(One_Fifty *Int_Par_Ref);
void Proc_3(Rec_Pointer *Ptr_Ref_Par);
void Proc_4(void);
void Proc_5(void);
void Proc_6(Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par);
void Proc_7(One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref);
void Proc_8(int *Arr_1_Par_Ref, int Arr_2_Par_Ref[][50], int Int_1_Par_Val, int Int_2_Par_Val);
Enumeration Func_1(Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val);
Boolean Func_2(char *Str_1_Par_Ref, char *Str_2_Par_Ref);
Boolean Func_3(Enumeration Enum_Par_Val);

/* ------------------------------------------------------------------ dhry_1.c / dhry_2.c */

void Proc_1(Rec_Pointer Ptr_Val_Par)
{
    REG Rec_Pointer Next_Record = Ptr_Val_Par->Ptr_Comp;

    *Ptr_Val_Par->Ptr_Comp = *Ptr_Glob;
    Ptr_Val_Par->variant.var_1.Int_Comp = 5;
    Next_Record->variant.var_1.Int_Comp = Ptr_Val_Par->variant.var_1.Int_Comp;
    Next_Record->Ptr_Comp = Ptr_Val_Par->Ptr_Comp;
    Proc_3(&Next_Record->Ptr_Comp);
    if (Next_Record->Discr == Ident_1) {
        Next_Record->variant.var_1.Int_Comp = 6;
        Proc_6(Ptr_Val_Par->variant.var_1.Enum_Comp, &Next_Record->variant.var_1.Enum_Comp);
        Next_Record->Ptr_Comp = Ptr_Glob->Ptr_Comp;
        Proc_7(Next_Record->variant.var_1.Int_Comp, 10, &Next_Record->variant.var_1.Int_Comp);
    } else
        *Ptr_Val_Par = *Ptr_Val_Par->Ptr_Comp;
}

void Proc_2(One_Fifty *Int_Par_Ref)
{
    One_Fifty Int_Loc;
    Enumeration Enum_Loc;

    Int_Loc = *Int_Par_Ref + 10;
    do
        if (Ch_1_Glob == 'A') {
            Int_Loc -= 1;
            *Int_Par_Ref = Int_Loc - Int_Glob;
            Enum_Loc = Ident_1;
        }
    while (Enum_Loc != Ident_1);
}

void Proc_3(Rec_Pointer *Ptr_Ref_Par)
{
    if (Ptr_Glob != Null)
        *Ptr_Ref_Par = Ptr_Glob->Ptr_Comp;
    Proc_7(10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
}

void Proc_4(void)
{
    Boolean Bool_Loc;

    Bool_Loc = Ch_1_Glob == 'A';
    Bool_Glob = Bool_Loc | Bool_Glob;
    Ch_2_Glob = 'B';
}

void Proc_5(void)
{
    Ch_1_Glob = 'A';
    Bool_Glob = false;
}

Boolean Func_3(Enumeration Enum_Par_Val)
{
    Enumeration Enum_Loc;

    Enum_Loc = Enum_Par_Val;
    if (Enum_Loc == Ident_3)
        return true;
    else
        return false;
}

void Proc_6(Enumeration Enum_Val_Par, Enumeration *Enum_Ref_Par)
{
    *Enum_Ref_Par = Enum_Val_Par;
    if (!Func_3(Enum_Val_Par))
        *Enum_Ref_Par = Ident_4;
    switch (Enum_Val_Par) {
    case Ident_1:
        *Enum_Ref_Par = Ident_1;
        break;
    case Ident_2:
        if (Int_Glob > 100)
            *Enum_Ref_Par = Ident_1;
        else
            *Enum_Ref_Par = Ident_4;
        break;
    case Ident_3:
        *Enum_Ref_Par = Ident_2;
        break;
    case Ident_4:
        break;
    case Ident_5:
        *Enum_Ref_Par = Ident_3;
        break;
    }
}

void Proc_7(One_Fifty Int_1_Par_Val, One_Fifty Int_2_Par_Val, One_Fifty *Int_Par_Ref)
{
    One_Fifty Int_Loc;

    Int_Loc = Int_1_Par_Val + 2;
    *Int_Par_Ref = Int_2_Par_Val + Int_Loc;
}

void Proc_8(int *Arr_1_Par_Ref, int Arr_2_Par_Ref[][50], int Int_1_Par_Val, int Int_2_Par_Val)
{
    REG One_Fifty Int_Index;
    REG One_Fifty Int_Loc;

    Int_Loc = Int_1_Par_Val + 5;
    Arr_1_Par_Ref[Int_Loc] = Int_2_Par_Val;
    Arr_1_Par_Ref[Int_Loc + 1] = Arr_1_Par_Ref[Int_Loc];
    Arr_1_Par_Ref[Int_Loc + 30] = Int_Loc;
    for (Int_Index = Int_Loc; Int_Index <= Int_Loc + 1; ++Int_Index)
        Arr_2_Par_Ref[Int_Loc][Int_Index] = Int_Loc;
    Arr_2_Par_Ref[Int_Loc][Int_Loc - 1] += 1;
    Arr_2_Par_Ref[Int_Loc + 20][Int_Loc] = Arr_1_Par_Ref[Int_Loc];
    Int_Glob = 5;
}

Enumeration Func_1(Capital_Letter Ch_1_Par_Val, Capital_Letter Ch_2_Par_Val)
{
    Capital_Letter Ch_1_Loc;
    Capital_Letter Ch_2_Loc;

    Ch_1_Loc = Ch_1_Par_Val;
    Ch_2_Loc = Ch_1_Loc;
    if (Ch_2_Loc != Ch_2_Par_Val)
        return Ident_1;
    else {
        Ch_1_Glob = Ch_1_Loc;
        return Ident_2;
    }
}

Boolean Func_2(char *Str_1_Par_Ref, char *Str_2_Par_Ref)
{
    REG One_Thirty Int_Loc;
    Capital_Letter Ch_Loc;

    Int_Loc = 2;
    while (Int_Loc <= 2)
        if (Func_1(Str_1_Par_Ref[Int_Loc], Str_2_Par_Ref[Int_Loc + 1]) == Ident_1) {
            Ch_Loc = 'A';
            Int_Loc += 1;
        }
    if (Ch_Loc >= 'W' && Ch_Loc < 'Z')
        Int_Loc = 7;
    if (Ch_Loc == 'R')
        return true;
    else {
        if (strcmp(Str_1_Par_Ref, Str_2_Par_Ref) > 0) {
            Int_Loc += 7;
            Int_Glob = Int_Loc;
            return true;
        } else
            return false;
    }
}

/* the body of the Dhrystone loop: passes first .. first + count - 1 */
void dhry_loop(unsigned long first, unsigned count)
{
    One_Fifty Int_1_Loc, Int_2_Loc, Int_3_Loc;
    Enumeration Enum_Loc;
    char Ch_Index;
    Str_30 Str_2_Loc;
    unsigned long Run_Index;
    unsigned long last = first + count;

    Int_1_Loc = Last_Int_1_Loc;
    Int_2_Loc = Last_Int_2_Loc;
    Int_3_Loc = Last_Int_3_Loc;
    Enum_Loc = Last_Enum_Loc;
    for (Run_Index = first; Run_Index < last; ++Run_Index) {
        Proc_5();
        Proc_4();
        Int_1_Loc = 2;
        Int_2_Loc = 3;
        strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        Enum_Loc = Ident_2;
        Bool_Glob = !Func_2(Str_1_Loc, Str_2_Loc);
        while (Int_1_Loc < Int_2_Loc) {
            Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
            Proc_7(Int_1_Loc, Int_2_Loc, &Int_3_Loc);
            Int_1_Loc += 1;
        }
        Proc_8(Arr_1_Glob, Arr_2_Glob, Int_1_Loc, Int_3_Loc);
        Proc_1(Ptr_Glob);
        for (Ch_Index = 'A'; Ch_Index <= Ch_2_Glob; ++Ch_Index) {
            if (Enum_Loc == Func_1(Ch_Index, 'C')) {
                Proc_6(Ident_1, &Enum_Loc);
                strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING");
                Int_2_Loc = Run_Index;
                Int_Glob = Run_Index;
            }
        }
        Int_2_Loc = Int_2_Loc * Int_1_Loc;
        Int_1_Loc = Int_2_Loc / Int_3_Loc;
        Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
        Proc_2(&Int_1_Loc);
    }
    Last_Int_1_Loc = Int_1_Loc;
    Last_Int_2_Loc = Int_2_Loc;
    Last_Int_3_Loc = Int_3_Loc;
    Last_Enum_Loc = Enum_Loc;
    strcpy(Last_Str_2_Loc, Str_2_Loc);
}

/* ------------------------------------------------------------------------ timing (MK09) */

static u16 last_tick;
static unsigned long ticks_total;

/* ticks since the start of the program (the kernel counter is 16 bits wide: add the differences) */
static unsigned long now(void)
{
    u16 t = mk_ticks();
    ticks_total += (u16)(t - last_tick);
    last_tick = t;
    return ticks_total;
}

static char nbuf[12];

static const char *ul(unsigned long v)           /* printf of MK09 has no %lu */
{
    char *p = nbuf + sizeof(nbuf) - 1;
    *p = 0;
    do {
        *--p = (char)('0' + (u16)(v % 10UL));
        v /= 10UL;
    } while (v);
    return p;
}

static void check(const char *what, int got, int want, int *bad)
{
    if (got != want) {
        printf("  WRONG %s: %d, should be %d\n", what, got, want);
        (*bad)++;
    }
}

static void checks(unsigned long runs)
{
    int bad = 0;

    puts("Final values of the variables used in the benchmark:\n");
    printf("Int_Glob:            %d\n", Int_Glob);                       check("Int_Glob", Int_Glob, 5, &bad);
    printf("Bool_Glob:           %d\n", Bool_Glob);                      check("Bool_Glob", Bool_Glob, 1, &bad);
    printf("Ch_1_Glob:           %c\n", Ch_1_Glob);                      check("Ch_1_Glob", Ch_1_Glob, 'A', &bad);
    printf("Ch_2_Glob:           %c\n", Ch_2_Glob);                      check("Ch_2_Glob", Ch_2_Glob, 'B', &bad);
    printf("Arr_1_Glob[8]:       %d\n", Arr_1_Glob[8]);                  check("Arr_1_Glob[8]", Arr_1_Glob[8], 7, &bad);
    printf("Arr_2_Glob[8][7]:    %d\n", Arr_2_Glob[8][7]);               check("Arr_2_Glob[8][7]", Arr_2_Glob[8][7], (int)(runs + 10), &bad);
    printf("Discr:               %d\n", Ptr_Glob->Discr);                check("Ptr_Glob->Discr", Ptr_Glob->Discr, 0, &bad);
    printf("Enum_Comp:           %d\n", Ptr_Glob->variant.var_1.Enum_Comp);
    check("Ptr_Glob->Enum_Comp", Ptr_Glob->variant.var_1.Enum_Comp, 2, &bad);
    printf("Int_Comp:            %d\n", Ptr_Glob->variant.var_1.Int_Comp);
    check("Ptr_Glob->Int_Comp", Ptr_Glob->variant.var_1.Int_Comp, 17, &bad);
    printf("Str_Comp:            %s\n", Ptr_Glob->variant.var_1.Str_Comp);
    if (strcmp(Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING")) { puts("  WRONG Str_Comp"); bad++; }
    printf("Next: Discr:         %d\n", Next_Ptr_Glob->Discr);           check("Next->Discr", Next_Ptr_Glob->Discr, 0, &bad);
    printf("Next: Enum_Comp:     %d\n", Next_Ptr_Glob->variant.var_1.Enum_Comp);
    check("Next->Enum_Comp", Next_Ptr_Glob->variant.var_1.Enum_Comp, 1, &bad);
    printf("Next: Int_Comp:      %d\n", Next_Ptr_Glob->variant.var_1.Int_Comp);
    check("Next->Int_Comp", Next_Ptr_Glob->variant.var_1.Int_Comp, 18, &bad);
    printf("Next: Str_Comp:      %s\n", Next_Ptr_Glob->variant.var_1.Str_Comp);
    if (strcmp(Next_Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING")) { puts("  WRONG Next Str_Comp"); bad++; }
    printf("Int_1_Loc:           %d\n", Last_Int_1_Loc);                 check("Int_1_Loc", Last_Int_1_Loc, 5, &bad);
    printf("Int_2_Loc:           %d\n", Last_Int_2_Loc);                 check("Int_2_Loc", Last_Int_2_Loc, 13, &bad);
    printf("Int_3_Loc:           %d\n", Last_Int_3_Loc);                 check("Int_3_Loc", Last_Int_3_Loc, 7, &bad);
    printf("Enum_Loc:            %d\n", Last_Enum_Loc);                  check("Enum_Loc", Last_Enum_Loc, 1, &bad);
    printf("Str_1_Loc:           %s\n", Str_1_Loc);
    if (strcmp(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING")) { puts("  WRONG Str_1_Loc"); bad++; }
    printf("Str_2_Loc:           %s\n", Last_Str_2_Loc);
    if (strcmp(Last_Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING")) { puts("  WRONG Str_2_Loc"); bad++; }
    puts(bad ? "\nDhrystone checks FAILED" : "\nDhrystone checks OK");
}

static unsigned long getnum(const char *s)
{
    unsigned long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10UL + (u16)(*s++ - '0');
    return v;
}

int main(int argc, char **argv)
{
    unsigned long runs = 0, fixed = 0, t0, t1, ticks, target = 10, dps;
    int i;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 't') {
            const char *v = argv[i] + 2;
            if (!*v && i + 1 < argc) v = argv[++i];
            target = getnum(v);
            if (target < 1) target = 1;
        } else
            fixed = getnum(argv[i]);
    }

    Next_Ptr_Glob = &Rec_Next;             /* the original uses malloc(); the records are static here */
    Ptr_Glob = &Rec_Glob;
    Ptr_Glob->Ptr_Comp = Next_Ptr_Glob;
    Ptr_Glob->Discr = Ident_1;
    Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
    Ptr_Glob->variant.var_1.Int_Comp = 40;
    strcpy(Ptr_Glob->variant.var_1.Str_Comp, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");
    Arr_2_Glob[8][7] = 10;

    puts("\nDhrystone Benchmark, Version 2.1 (Language: C, CMOC on MK09)\n");
    last_tick = mk_ticks();

    t0 = now();
    if (fixed) {
        while (runs < fixed) {
            unsigned n = fixed - runs > 10 ? 10 : (unsigned)(fixed - runs);
            dhry_loop(runs + 1, n);
            runs += n;
        }
    } else {
        for (;;) {                          /* chunks of 10 passes (about 50 ms) */
            dhry_loop(runs + 1, 10);
            runs += 10;
            if (now() - t0 >= target * TICK_HZ) break;
        }
    }
    t1 = now();
    ticks = t1 - t0;
    checks(runs);
    printf("\nRuns:                  %s\n", ul(runs));
    printf("Time:                  %s ms\n", ul(ticks * (1000UL / TICK_HZ)));
    if (ticks && runs) {
        dps = runs * (unsigned long)TICK_HZ / ticks;
        printf("Microseconds for one run through Dhrystone: %s\n", ul(ticks * (1000000UL / TICK_HZ) / runs));
        printf("Dhrystones per second: %s\n", ul(dps));
        dps = dps * 100UL / 1757UL;
        printf("VAX MIPS rating (Dhrystones/s / 1757): %s.%s%s\n", ul(dps / 100UL), dps % 100UL < 10UL ? "0" : "", ul(dps % 100UL));
    }
    return 0;
}
