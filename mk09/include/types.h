/* SPDX-License-Identifier: MIT */
/* types.h - fixed size integer types of MK09 (kernel, servers, applications) */
#ifndef MK_TYPES_H
#define MK_TYPES_H

#ifdef MK_HOST                      /* host side tests of the portable parts */
#include <stdint.h>
typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
#else                               /* CMOC: char 8, int 16, long 32 bits */
typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned int   u16;
typedef int            s16;
typedef unsigned long  u32;
typedef long           s32;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
