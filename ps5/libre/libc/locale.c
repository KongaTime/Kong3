/*
 * RPCS3 for PS5 - locales: the C locale, and the _l functions on it.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds each
 * name to its libre_ function. The console's C library gives a title no
 * locale_t, none of the _l functions and none of FreeBSD's rune tables, which
 * the SDK's headers (FreeBSD's) and libc++ built on them call. Written from
 * POSIX's locales and FreeBSD's xlocale(3), on the calls the console does give:
 *
 * - One locale exists, the C (or POSIX) locale: newlocale gives it for "C",
 *   "POSIX" and "" (the environment's, which is C on the console) whatever the
 *   mask and base, and fails for any other name (ENOENT); freelocale has
 *   nothing to free. Every _l function takes any locale_t (LC_GLOBAL_LOCALE
 *   and NULL too) as the C locale and calls the console's own function.
 * - The rune table libc++'s inline classifiers read (__runes_for_locale) is
 *   built once from the console's own wide character functions (isw*, tow*),
 *   so it agrees with the mbrtowc and friends the _l functions call.
 * - The encoding is the one the console's conversions use, tried once: UTF-8
 *   if mbrtowc decodes a two-byte sequence, as it does, though the console's
 *   MB_CUR_MAX says 1 (build 169, on my console). libc++ sizes its conversions
 *   by ___mb_cur_max_l, so that gives 4 then (else 1), nl_langinfo's CODESET
 *   says "UTF-8", and the rune table's single-byte limit is 128: bytes from 128
 *   begin multibyte characters.
 * - localeconv and nl_langinfo report the C locale: "." the decimal point (the
 *   console's localeconv gives none, and tinygltf's JSON parser then read every
 *   glTF fraction as 0), English names, POSIX's formats.
 * - mbsnrtowcs_l and wcsnrtombs_l (the console exports neither mbsnrtowcs nor
 *   wcsnrtombs) on mbrtowc and wcrtomb; asprintf_l on the title's heap, as asprintf's caller frees.
 * - No message catalogs: catopen fails (ENOENT) and catgets gives the default.
 */

#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <nl_types.h>
#include <pthread.h>
#include <runetype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <xlocale.h>

/* The SDK's ctype and wctype headers make the classifiers macros on FreeBSD's
 * rune tables, which the console does not export: its functions, here */
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
int iswalpha(wint_t);
int iswblank(wint_t);
int iswcntrl(wint_t);
int iswctype(wint_t, wctype_t);
int iswdigit(wint_t);
int iswgraph(wint_t);
int iswlower(wint_t);
int iswprint(wint_t);
int iswpunct(wint_t);
int iswspace(wint_t);
int iswupper(wint_t);
int iswxdigit(wint_t);
wint_t towlower(wint_t);
wint_t towupper(wint_t);

/* The one locale */
struct _xlocale
{
   int c;
};
static struct _xlocale g_c_locale = {1};

locale_t libre_newlocale(int mask, const char *name, locale_t base)
{
   (void)base;
   if (mask & ~LC_ALL_MASK)
   {
      errno = EINVAL;
      return NULL;
   }
   if (!name || !(strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0 || !*name))
   {
      errno = name ? ENOENT : EINVAL;
      return NULL;
   }
   return &g_c_locale;
}

int libre_freelocale(locale_t locale)
{
   (void)locale;
   return 0;
}

/* The rune table, from the console's classifiers */
static _RuneLocale g_runes;
static int g_utf8;
static pthread_once_t g_runes_once = PTHREAD_ONCE_INIT;

static unsigned long rune_type(wint_t c)
{
   unsigned long type = 0;
   if (iswalpha(c))
      type |= _CTYPE_A;
   if (iswcntrl(c))
      type |= _CTYPE_C;
   if (iswdigit(c))
      type |= _CTYPE_D;
   if (iswgraph(c))
      type |= _CTYPE_G;
   if (iswlower(c))
      type |= _CTYPE_L;
   if (iswpunct(c))
      type |= _CTYPE_P;
   if (iswspace(c))
      type |= _CTYPE_S;
   if (iswupper(c))
      type |= _CTYPE_U;
   if (iswxdigit(c))
      type |= _CTYPE_X;
   if (iswblank(c))
      type |= _CTYPE_B;
   if (iswprint(c))
      type |= _CTYPE_R;
   return type;
}

static void build_runes(void)
{
   mbstate_t state;
   memset(&state, 0, sizeof state);
   wchar_t c;
   g_utf8 = mbrtowc(&c, "\xc3\xa9", 2, &state) == 2 && c == 0xe9;
   memcpy(g_runes.__magic, _RUNE_MAGIC_1, sizeof g_runes.__magic);
   snprintf(g_runes.__encoding, sizeof g_runes.__encoding, "%s", g_utf8 ? "UTF-8" : "NONE");
   g_runes.__invalid_rune = 0xfffd;
   for (int c = 0; c < _CACHED_RUNES; c++)
   {
      g_runes.__runetype[c] = rune_type((wint_t)c);
      g_runes.__maplower[c] = (__rune_t)towlower((wint_t)c);
      g_runes.__mapupper[c] = (__rune_t)towupper((wint_t)c);
   }
}

_RuneLocale *libre___runes_for_locale(locale_t locale, int *single_byte_limit)
{
   (void)locale;
   pthread_once(&g_runes_once, build_runes);
   if (single_byte_limit)
      *single_byte_limit = g_utf8 ? 128 : 256;
   return &g_runes;
}

unsigned long libre____runetype_l(__ct_rune_t c, locale_t locale)
{
   (void)locale;
   return c < 0 ? 0 : rune_type((wint_t)c);
}

__ct_rune_t libre____tolower_l(__ct_rune_t c, locale_t locale)
{
   (void)locale;
   return c < 0 ? c : (__ct_rune_t)towlower((wint_t)c);
}

__ct_rune_t libre____toupper_l(__ct_rune_t c, locale_t locale)
{
   (void)locale;
   return c < 0 ? c : (__ct_rune_t)towupper((wint_t)c);
}

int libre____mb_cur_max_l(locale_t locale)
{
   (void)locale;
   pthread_once(&g_runes_once, build_runes);
   return g_utf8 ? 4 : 1;
}

int libre_iswctype_l(wint_t c, wctype_t type, locale_t locale)
{
   (void)locale;
   return iswctype(c, type);
}

/* The C locale's conventions and names */
static char g_empty[] = "";
static char g_point[] = ".";
static struct lconv g_conventions = {
   .decimal_point = g_point,
   .thousands_sep = g_empty,
   .grouping = g_empty,
   .int_curr_symbol = g_empty,
   .currency_symbol = g_empty,
   .mon_decimal_point = g_empty,
   .mon_thousands_sep = g_empty,
   .mon_grouping = g_empty,
   .positive_sign = g_empty,
   .negative_sign = g_empty,
   .int_frac_digits = CHAR_MAX,
   .frac_digits = CHAR_MAX,
   .p_cs_precedes = CHAR_MAX,
   .p_sep_by_space = CHAR_MAX,
   .n_cs_precedes = CHAR_MAX,
   .n_sep_by_space = CHAR_MAX,
   .p_sign_posn = CHAR_MAX,
   .n_sign_posn = CHAR_MAX,
   .int_p_cs_precedes = CHAR_MAX,
   .int_n_cs_precedes = CHAR_MAX,
   .int_p_sep_by_space = CHAR_MAX,
   .int_n_sep_by_space = CHAR_MAX,
   .int_p_sign_posn = CHAR_MAX,
   .int_n_sign_posn = CHAR_MAX,
};

struct lconv *libre_localeconv(void)
{
   return &g_conventions;
}

struct lconv *libre_localeconv_l(locale_t locale)
{
   (void)locale;
   return &g_conventions;
}

static const char *const g_days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const g_short_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const g_months[] = {"January", "February", "March", "April", "May", "June", "July", "August",
   "September", "October", "November", "December"};
static const char *const g_short_months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
   "Nov", "Dec"};

char *libre_nl_langinfo(nl_item item)
{
   const char *text = "";
   if (item >= DAY_1 && item <= DAY_7)
      text = g_days[item - DAY_1];
   else if (item >= ABDAY_1 && item <= ABDAY_7)
      text = g_short_days[item - ABDAY_1];
   else if (item >= MON_1 && item <= MON_12)
      text = g_months[item - MON_1];
   else if (item >= ABMON_1 && item <= ABMON_12)
      text = g_short_months[item - ABMON_1];
   else if (item >= ALTMON_1 && item <= ALTMON_12)
      text = g_months[item - ALTMON_1];
   else
   {
      switch (item)
      {
      case CODESET:
         pthread_once(&g_runes_once, build_runes);
         text = g_utf8 ? "UTF-8" : "US-ASCII";
         break;
      case D_T_FMT:
         text = "%a %b %e %H:%M:%S %Y";
         break;
      case D_FMT:
         text = "%m/%d/%y";
         break;
      case T_FMT:
         text = "%H:%M:%S";
         break;
      case T_FMT_AMPM:
         text = "%I:%M:%S %p";
         break;
      case AM_STR:
         text = "AM";
         break;
      case PM_STR:
         text = "PM";
         break;
      case RADIXCHAR:
         text = ".";
         break;
      case YESEXPR:
         text = "^[yY]";
         break;
      case NOEXPR:
         text = "^[nN]";
         break;
      case YESSTR:
         text = "yes";
         break;
      case NOSTR:
         text = "no";
         break;
      case D_MD_ORDER:
         text = "md";
         break;
      default: /* ERA and its formats, ALT_DIGITS, THOUSEP, CRNCYSTR: none */
         break;
      }
   }
   return (char *)text;
}

char *libre_nl_langinfo_l(nl_item item, locale_t locale)
{
   (void)locale;
   return libre_nl_langinfo(item);
}

/* Numbers */
double libre_strtod_l(const char *restrict text, char **restrict end, locale_t locale)
{
   (void)locale;
   return strtod(text, end);
}

float libre_strtof_l(const char *restrict text, char **restrict end, locale_t locale)
{
   (void)locale;
   return strtof(text, end);
}

long double libre_strtold_l(const char *restrict text, char **restrict end, locale_t locale)
{
   (void)locale;
   return strtold(text, end);
}

long long libre_strtoll_l(const char *restrict text, char **restrict end, int base, locale_t locale)
{
   (void)locale;
   return strtoll(text, end, base);
}

unsigned long long libre_strtoull_l(const char *restrict text, char **restrict end, int base, locale_t locale)
{
   (void)locale;
   return strtoull(text, end, base);
}

/* Formatting */
int libre_snprintf_l(char *restrict buffer, size_t size, locale_t locale, const char *restrict format, ...)
{
   (void)locale;
   va_list arguments;
   va_start(arguments, format);
   const int length = vsnprintf(buffer, size, format, arguments);
   va_end(arguments);
   return length;
}

int libre_sscanf_l(const char *restrict text, locale_t locale, const char *restrict format, ...)
{
   (void)locale;
   va_list arguments;
   va_start(arguments, format);
   const int count = vsscanf(text, format, arguments);
   va_end(arguments);
   return count;
}

int libre_asprintf_l(char **result, locale_t locale, const char *format, ...)
{
   (void)locale;
   va_list arguments, again;
   va_start(arguments, format);
   va_copy(again, arguments);
   const int length = vsnprintf(NULL, 0, format, arguments);
   va_end(arguments);
   char *text = length >= 0 ? malloc((size_t)length + 1) : NULL;
   if (text)
      vsnprintf(text, (size_t)length + 1, format, again);
   va_end(again);
   *result = text;
   if (!text)
   {
      if (length >= 0)
         errno = ENOMEM;
      return -1;
   }
   return length;
}

size_t libre_strftime_l(char *restrict buffer, size_t size, const char *restrict format, const struct tm *restrict time,
   locale_t locale)
{
   (void)locale;
   return strftime(buffer, size, format, time);
}

/* Collation */
int libre_strcoll_l(const char *a, const char *b, locale_t locale)
{
   (void)locale;
   return strcoll(a, b);
}

size_t libre_strxfrm_l(char *restrict out, const char *restrict in, size_t size, locale_t locale)
{
   (void)locale;
   return strxfrm(out, in, size);
}

int libre_wcscoll_l(const wchar_t *a, const wchar_t *b, locale_t locale)
{
   (void)locale;
   return wcscoll(a, b);
}

size_t libre_wcsxfrm_l(wchar_t *restrict out, const wchar_t *restrict in, size_t size, locale_t locale)
{
   (void)locale;
   return wcsxfrm(out, in, size);
}

/* Multibyte and wide characters */
wint_t libre_btowc_l(int c, locale_t locale)
{
   (void)locale;
   return btowc(c);
}

int libre_wctob_l(wint_t c, locale_t locale)
{
   (void)locale;
   return wctob(c);
}

size_t libre_mbrlen_l(const char *restrict s, size_t n, mbstate_t *restrict state, locale_t locale)
{
   (void)locale;
   return mbrlen(s, n, state);
}

size_t libre_mbrtowc_l(wchar_t *restrict out, const char *restrict s, size_t n, mbstate_t *restrict state,
   locale_t locale)
{
   (void)locale;
   return mbrtowc(out, s, n, state);
}

int libre_mbtowc_l(wchar_t *restrict out, const char *restrict s, size_t n, locale_t locale)
{
   (void)locale;
   return mbtowc(out, s, n);
}

size_t libre_wcrtomb_l(char *restrict out, wchar_t c, mbstate_t *restrict state, locale_t locale)
{
   (void)locale;
   return wcrtomb(out, c, state);
}

size_t libre_mbsrtowcs_l(wchar_t *restrict out, const char **restrict in, size_t length, mbstate_t *restrict state,
   locale_t locale)
{
   (void)locale;
   return mbsrtowcs(out, in, length, state);
}

/* At most limit bytes of *in, at most length wide characters to out; with no
 * out, only counted, *in and the state left as they were */
static size_t to_wide(wchar_t *restrict out, const char **restrict in, size_t limit, size_t length,
   mbstate_t *restrict state)
{
   static mbstate_t own;
   mbstate_t counting;
   if (!state)
      state = &own;
   if (!out)
   {
      counting = *state;
      state = &counting;
   }
   const char *s = *in;
   size_t count = 0;
   while (limit && (!out || count < length))
   {
      wchar_t c;
      const size_t used = mbrtowc(&c, s, limit, state);
      if (used == (size_t)-1)
      {
         if (out)
            *in = s;
         return (size_t)-1;
      }
      if (used == (size_t)-2)
      {
         /* A character begun but not ended within the limit: kept in the state */
         s += limit;
         break;
      }
      if (used == 0)
      {
         if (out)
         {
            out[count] = 0;
            *in = NULL;
         }
         return count;
      }
      if (out)
         out[count] = c;
      s += used;
      limit -= used;
      count++;
   }
   if (out)
      *in = s;
   return count;
}

size_t libre_mbsnrtowcs_l(wchar_t *restrict out, const char **restrict in, size_t limit, size_t length,
   mbstate_t *restrict state, locale_t locale)
{
   (void)locale;
   return to_wide(out, in, limit, length, state);
}

/* At most limit wide characters of *in, at most length bytes to out, never part
 * of a character; with no out, only counted, *in and the state left as they were */
static size_t to_multibyte(char *restrict out, const wchar_t **restrict in, size_t limit, size_t length,
   mbstate_t *restrict state)
{
   static mbstate_t own;
   if (!state)
      state = &own;
   const wchar_t *s = *in;
   size_t count = 0;
   mbstate_t now = *state;
   while (limit)
   {
      char bytes[MB_LEN_MAX];
      mbstate_t next = now;
      const size_t made = wcrtomb(bytes, *s, &next);
      if (made == (size_t)-1)
      {
         if (out)
         {
            *in = s;
            *state = now;
         }
         return (size_t)-1;
      }
      if (out)
      {
         if (count + made > length)
            break;
         memcpy(out + count, bytes, made);
      }
      now = next;
      if (*s == 0)
      {
         /* The end: the NUL written but not counted */
         if (out)
         {
            *in = NULL;
            *state = now;
         }
         return count + made - 1;
      }
      count += made;
      s++;
      limit--;
   }
   if (out)
   {
      *in = s;
      *state = now;
   }
   return count;
}

size_t libre_wcsnrtombs_l(char *restrict out, const wchar_t **restrict in, size_t limit, size_t length,
   mbstate_t *restrict state, locale_t locale)
{
   (void)locale;
   return to_multibyte(out, in, limit, length, state);
}

/* Message catalogs: none */
nl_catd libre_catopen(const char *name, int flags)
{
   (void)name, (void)flags;
   errno = ENOENT;
   return (nl_catd)-1;
}

char *libre_catgets(nl_catd catalog, int set, int number, const char *message)
{
   (void)catalog, (void)set, (void)number;
   return (char *)message;
}

int libre_catclose(nl_catd catalog)
{
   if (catalog == (nl_catd)-1 || !catalog)
   {
      errno = EBADF;
      return -1;
   }
   return 0;
}
