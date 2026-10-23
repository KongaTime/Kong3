/*
 * RPCS3 for PS5 - regular expressions.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds
 * regcomp, regexec, regfree and regerror to these libre_ ones. The console's
 * modules give a title none of them. Written from POSIX and FreeBSD's regex(3)
 * (the console's regex.h is FreeBSD's) on musl's engine (libc/musl_regex.h):
 *
 * - FreeBSD's regex_t holds the musl one it compiled to (re_g); flags and
 *   matches are translated, the errors are the same numbers.
 * - FreeBSD's additions: REG_PEND (the pattern ends at re_endp), REG_NOSPEC
 *   (every character literal) and REG_STARTEND (the string is pmatch[0]'s range,
 *   and the offsets are the string's). A pattern or string ends at its first
 *   NUL even so: musl's engine stops there.
 * - regerror's messages are POSIX's meanings, REG_ITOA names a code and REG_ATOI
 *   finds the code of a name (re_endp), as FreeBSD's do.
 */

#include "musl_regex.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC 0x6c726567 /* "lreg" */

/* What re_g points at: the compiled expression and whether it keeps matches */
struct compiled
{
   libre_musl_regex_t musl;
   int no_matches;
};

int libre_regcomp(regex_t *re, const char *pattern, int flags)
{
   if (!re || !pattern)
      return REG_INVARG;
   const char *end = (flags & REG_PEND) ? re->re_endp : pattern + strlen(pattern);
   if (!end || end < pattern)
      return REG_INVARG;
   const size_t length = (size_t)(end - pattern);

   /* The pattern as musl takes it: ended by a NUL, and with REG_NOSPEC a basic
    * expression with every special character escaped */
   char *copy = NULL;
   int musl_flags = 0;
   if (flags & REG_NOSPEC)
   {
      if (!(copy = malloc(2 * length + 1)))
         return REG_ESPACE;
      char *out = copy;
      for (size_t i = 0; i < length && pattern[i]; i++)
      {
         if (strchr(".[\\*^$", pattern[i]))
            *out++ = '\\';
         *out++ = pattern[i];
      }
      *out = 0;
   }
   else
   {
      if (flags & REG_EXTENDED)
         musl_flags |= LIBRE_MUSL_REG_EXTENDED;
      if ((flags & REG_PEND) && !(copy = strndup(pattern, length)))
         return REG_ESPACE;
   }
   if (flags & REG_ICASE)
      musl_flags |= LIBRE_MUSL_REG_ICASE;
   if (flags & REG_NEWLINE)
      musl_flags |= LIBRE_MUSL_REG_NEWLINE;
   if (flags & REG_NOSUB)
      musl_flags |= LIBRE_MUSL_REG_NOSUB;

   struct compiled *compiled = calloc(1, sizeof *compiled);
   if (!compiled)
   {
      free(copy);
      return REG_ESPACE;
   }
   const int error = libre_musl_regcomp(&compiled->musl, copy ? copy : pattern, musl_flags);
   free(copy);
   if (error)
   {
      free(compiled);
      return error;
   }
   compiled->no_matches = (flags & REG_NOSUB) != 0;
   re->re_magic = MAGIC;
   re->re_nsub = compiled->musl.re_nsub;
   re->re_g = (struct re_guts *)compiled;
   return 0;
}

int libre_regexec(const regex_t *re, const char *string, size_t count, regmatch_t matches[], int flags)
{
   if (!re || re->re_magic != MAGIC || !re->re_g)
      return REG_BADPAT;
   if (!string)
      return REG_INVARG;
   struct compiled *compiled = (struct compiled *)re->re_g;

   /* REG_STARTEND: the range pmatch[0] gives, as a string of its own */
   regoff_t start = 0;
   char *range = NULL;
   if (flags & REG_STARTEND)
   {
      if (!matches || matches[0].rm_so < 0 || matches[0].rm_eo < matches[0].rm_so)
         return REG_INVARG;
      start = matches[0].rm_so;
      if (!(range = strndup(string + start, (size_t)(matches[0].rm_eo - start))))
         return REG_ESPACE;
   }
   if (compiled->no_matches || !matches)
      count = 0;

   libre_musl_regmatch_t local[16];
   libre_musl_regmatch_t *found = local;
   if (count > sizeof local / sizeof local[0] && !(found = malloc(count * sizeof *found)))
   {
      free(range);
      return REG_ESPACE;
   }
   int musl_flags = 0;
   if (flags & REG_NOTBOL)
      musl_flags |= LIBRE_MUSL_REG_NOTBOL;
   if (flags & REG_NOTEOL)
      musl_flags |= LIBRE_MUSL_REG_NOTEOL;
   const int result = libre_musl_regexec(&compiled->musl, range ? range : string, count, found, musl_flags);
   if (result == 0)
   {
      for (size_t i = 0; i < count; i++)
      {
         matches[i].rm_so = found[i].rm_so < 0 ? -1 : found[i].rm_so + start;
         matches[i].rm_eo = found[i].rm_eo < 0 ? -1 : found[i].rm_eo + start;
      }
   }
   if (found != local)
      free(found);
   free(range);
   return result;
}

void libre_regfree(regex_t *re)
{
   if (!re || re->re_magic != MAGIC || !re->re_g)
      return;
   struct compiled *compiled = (struct compiled *)re->re_g;
   libre_musl_regfree(&compiled->musl);
   free(compiled);
   re->re_g = NULL;
   re->re_magic = 0;
}

static const struct
{
   int code;
   const char *name;
   const char *message;
} g_errors[] = {
   {0, "REG_NOERROR", "success"},
   {REG_NOMATCH, "REG_NOMATCH", "regexec() failed to match"},
   {REG_BADPAT, "REG_BADPAT", "invalid regular expression"},
   {REG_ECOLLATE, "REG_ECOLLATE", "invalid collating element"},
   {REG_ECTYPE, "REG_ECTYPE", "invalid character class"},
   {REG_EESCAPE, "REG_EESCAPE", "trailing backslash"},
   {REG_ESUBREG, "REG_ESUBREG", "invalid back reference"},
   {REG_EBRACK, "REG_EBRACK", "brackets ([ ]) not balanced"},
   {REG_EPAREN, "REG_EPAREN", "parentheses not balanced"},
   {REG_EBRACE, "REG_EBRACE", "braces not balanced"},
   {REG_BADBR, "REG_BADBR", "invalid repetition count(s)"},
   {REG_ERANGE, "REG_ERANGE", "invalid character range"},
   {REG_ESPACE, "REG_ESPACE", "out of memory"},
   {REG_BADRPT, "REG_BADRPT", "repetition-operator operand invalid"},
   {REG_EMPTY, "REG_EMPTY", "empty (sub)expression"},
   {REG_ASSERT, "REG_ASSERT", "cannot happen"},
   {REG_INVARG, "REG_INVARG", "invalid argument to regex routine"},
   {REG_ILLSEQ, "REG_ILLSEQ", "illegal byte sequence"},
};

size_t libre_regerror(int code, const regex_t *re, char *buffer, size_t size)
{
   char number[32];
   const char *text = NULL;
   const size_t known = sizeof g_errors / sizeof g_errors[0];
   if (code == REG_ATOI)
   {
      /* The code of the name at re_endp, or 0 */
      int found = 0;
      for (size_t i = 0; re && re->re_endp && i < known; i++)
      {
         if (strcmp(g_errors[i].name, re->re_endp) == 0)
         {
            found = g_errors[i].code;
            break;
         }
      }
      snprintf(number, sizeof number, "%d", found);
      text = number;
   }
   else
   {
      const int bare = code & ~REG_ITOA;
      for (size_t i = 0; i < known && !text; i++)
      {
         if (g_errors[i].code == bare)
            text = (code & REG_ITOA) ? g_errors[i].name : g_errors[i].message;
      }
      if (!text)
      {
         if (code & REG_ITOA)
            snprintf(number, sizeof number, "REG_0x%x", bare);
         else
            snprintf(number, sizeof number, "unknown regexp error %d", bare);
         text = number;
      }
   }
   const size_t length = strlen(text);
   if (buffer && size)
   {
      const size_t copied = length < size - 1 ? length : size - 1;
      memcpy(buffer, text, copied);
      buffer[copied] = 0;
   }
   return length + 1;
}
