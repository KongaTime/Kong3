/*
 * RPCS3 for PS5 - the regex.h musl's regular expressions are built with.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). regcomp.c, regexec.c,
 * tre-mem.c and tre.h here are musl 1.2.5's src/regex, unchanged (MIT,
 * COPYRIGHT). They include <regex.h>, and the build puts this folder first so
 * they find this one: musl's interface (libc/musl_regex.h) under the libre
 * layer's names, and musl's "hidden" for the engine's own functions (which
 * tre.h names __tre_mem_*).
 */

#ifndef LIBRE_MUSL_REGEX_SHIM_H
#define LIBRE_MUSL_REGEX_SHIM_H

#include "../../libc/musl_regex.h"

/* The wide character classes as the console's functions: the SDK's wctype.h
 * (FreeBSD's) makes them macros on FreeBSD's rune tables, which the console's
 * C library does not export (its ctype.h leaves them out for the console,
 * __SCE__, its wctype.h does not). Included here, first, so they stay out */
#include <wctype.h>
#undef iswalnum
#undef iswalpha
#undef iswblank
#undef iswcntrl
#undef iswctype
#undef iswdigit
#undef iswgraph
#undef iswlower
#undef iswprint
#undef iswpunct
#undef iswspace
#undef iswupper
#undef iswxdigit
#undef towlower
#undef towupper

#define regex_t libre_musl_regex_t
#define regmatch_t libre_musl_regmatch_t
#define regoff_t libre_musl_regoff_t

#define REG_EXTENDED LIBRE_MUSL_REG_EXTENDED
#define REG_ICASE LIBRE_MUSL_REG_ICASE
#define REG_NEWLINE LIBRE_MUSL_REG_NEWLINE
#define REG_NOSUB LIBRE_MUSL_REG_NOSUB
#define REG_NOTBOL LIBRE_MUSL_REG_NOTBOL
#define REG_NOTEOL LIBRE_MUSL_REG_NOTEOL

#define REG_OK 0
#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ECOLLATE 3
#define REG_ECTYPE 4
#define REG_EESCAPE 5
#define REG_ESUBREG 6
#define REG_EBRACK 7
#define REG_EPAREN 8
#define REG_EBRACE 9
#define REG_BADBR 10
#define REG_ERANGE 11
#define REG_ESPACE 12
#define REG_BADRPT 13

#define regcomp libre_musl_regcomp
#define regexec libre_musl_regexec
#define regfree libre_musl_regfree

#define hidden __attribute__((__visibility__("hidden")))

#endif
