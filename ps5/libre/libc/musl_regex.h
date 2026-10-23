/*
 * RPCS3 for PS5 - musl's regular expressions, as the PS5 libre layer names them.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). musl 1.2.5's engine
 * (third_party/musl-regex, MIT) is built under these names, with musl's own
 * types and flags, so that it never meets the console's regex.h (FreeBSD's
 * types and flags), which libc/regex.c translates to it. Its layout is musl's
 * include/regex.h for a 64-bit target.
 */

#ifndef LIBRE_MUSL_REGEX_H
#define LIBRE_MUSL_REGEX_H

#include <stddef.h>

typedef long libre_musl_regoff_t;

typedef struct
{
   size_t re_nsub;
   void *__opaque, *__padding[4];
   size_t __nsub2;
   char __padding2;
} libre_musl_regex_t;

typedef struct
{
   libre_musl_regoff_t rm_so;
   libre_musl_regoff_t rm_eo;
} libre_musl_regmatch_t;

/* regcomp's flags */
#define LIBRE_MUSL_REG_EXTENDED 1
#define LIBRE_MUSL_REG_ICASE 2
#define LIBRE_MUSL_REG_NEWLINE 4
#define LIBRE_MUSL_REG_NOSUB 8

/* regexec's */
#define LIBRE_MUSL_REG_NOTBOL 1
#define LIBRE_MUSL_REG_NOTEOL 2

/* The errors, 1 to 13, are FreeBSD's numbers too */

int libre_musl_regcomp(libre_musl_regex_t *re, const char *pattern, int flags);
int libre_musl_regexec(const libre_musl_regex_t *re, const char *string, size_t count, libre_musl_regmatch_t *matches,
   int flags);
void libre_musl_regfree(libre_musl_regex_t *re);

#endif
