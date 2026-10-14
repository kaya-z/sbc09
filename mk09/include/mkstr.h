/* SPDX-License-Identifier: MIT */
/* mkstr.h - the few string and memory functions that MK09 programs use */
#ifndef MKSTR_H
#define MKSTR_H

#include "types.h"

#ifdef MK_HOST
#include <string.h>
#else
void *memcpy(void *dst, const void *src, u16 n);
void *memmove(void *dst, const void *src, u16 n);
void *memset(void *s, int c, u16 n);
int   memcmp(const void *a, const void *b, u16 n);
u16   strlen(const char *s);
char *strcpy(char *d, const char *s);
char *strcat(char *d, const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, u16 n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
#endif

#endif
