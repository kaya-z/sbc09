/* SPDX-License-Identifier: MIT */
/* lib/mk_strerr.c - part of the MK09 C library: the text of an error code */
#include "types.h"
#include "mklib.h"

const char *strerr_(int e)
{
    if (e < 0) e = -e;
    switch (e) {
    case E_PERM:     return "not allowed";
    case E_NOTASK:   return "no such task";
    case E_DEAD:     return "server is gone";
    case E_FAULT:    return "bad address";
    case E_NOSLOT:   return "no free task slot";
    case E_NOMEM:    return "no memory";
    case E_INVAL:    return "bad argument";
    case E_NOENT:    return "not found";
    case E_EXIST:    return "already exists";
    case E_NOTDIR:   return "not a directory";
    case E_ISDIR:    return "is a directory";
    case E_NOSPC:    return "disk full";
    case E_IO:       return "I/O error";
    case E_BADF:     return "bad file";
    case E_MFILE:    return "too many open files";
    case E_NOTEMPTY: return "directory not empty";
    case E_NAME:     return "bad name";
    case E_FORMAT:   return "bad format";
    case E_BUSY:     return "busy";
    case E_RO:       return "read only";
    case E_NOSYS:    return "not supported";
    }
    return "error";
}
