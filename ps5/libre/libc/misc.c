/*
 * RPCS3 for PS5 - C library functions a title lacks: the small ones.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds each
 * name to its libre_ function. None of these is exported to a title by the
 * console's modules (or only by modules whose imports are null for a title), so
 * each is written here from the C and POSIX standards and FreeBSD's
 * documentation, on the calls the console does give a title, or refuses what a
 * title cannot do (no processes, no users, no terminals) as the standards say.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

struct passwd;

extern const char *const sys_siglist[];
extern uint64_t sceKernelReadTsc(void);

/* The title's image, from the linker script (ps5/libre/ld/ps5-title.ld) */
extern char __libre_image_start[];
extern char __libre_image_end[];

/* Randomness: the kernel's own generator (FreeBSD's kern.arandom), 256 bytes at
 * a time into a buffer handed out under a lock */
static pthread_mutex_t g_random_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char g_random[256];
static size_t g_random_left;

static void random_fill(void)
{
   int name[2] = {CTL_KERN, KERN_ARND};
   size_t got = 0;
   while (got < sizeof g_random)
   {
      size_t size = sizeof g_random - got;
      if (sysctl(name, 2, g_random + got, &size, NULL, 0) != 0 || !size)
         break;
      got += size;
   }
   if (got < sizeof g_random)
   {
      /* Not given: the time-stamp counter, mixed (splitmix64) - unpredictable
       * enough for hashing seeds, not for keys */
      uint64_t state = sceKernelReadTsc() ^ (uint64_t)(uintptr_t)&state;
      for (size_t i = got; i < sizeof g_random; i++)
      {
         uint64_t z = (state += 0x9e3779b97f4a7c15ull);
         z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
         z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
         g_random[i] = (unsigned char)(z ^ (z >> 31));
      }
   }
   g_random_left = sizeof g_random;
}

void libre_arc4random_buf(void *buffer, size_t size)
{
   unsigned char *to = buffer;
   pthread_mutex_lock(&g_random_lock);
   while (size)
   {
      if (!g_random_left)
         random_fill();
      const size_t take = size < g_random_left ? size : g_random_left;
      unsigned char *from = g_random + sizeof g_random - g_random_left;
      memcpy(to, from, take);
      memset(from, 0, take); /* never handed out twice */
      g_random_left -= take;
      to += take;
      size -= take;
   }
   pthread_mutex_unlock(&g_random_lock);
}

uint32_t libre_arc4random(void)
{
   uint32_t value;
   libre_arc4random_buf(&value, sizeof value);
   return value;
}

uint32_t libre_arc4random_uniform(uint32_t upper)
{
   if (upper < 2)
      return 0;
   /* Values below 2^32 mod upper would make some results likelier: drawn again */
   const uint32_t least = (uint32_t)(-upper) % upper;
   for (;;)
   {
      const uint32_t value = libre_arc4random();
      if (value >= least)
         return value % upper;
   }
}

/* Time */

int libre_clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain)
{
   if (!request || request->tv_nsec < 0 || request->tv_nsec >= 1000000000)
      return EINVAL;
   if (!(flags & TIMER_ABSTIME))
      return nanosleep(request, remain) == 0 ? 0 : errno;
   for (;;)
   {
      struct timespec now;
      if (clock_gettime(clock, &now) != 0)
         return errno;
      if (now.tv_sec > request->tv_sec || (now.tv_sec == request->tv_sec && now.tv_nsec >= request->tv_nsec))
         return 0;
      struct timespec wait = {request->tv_sec - now.tv_sec, request->tv_nsec - now.tv_nsec};
      if (wait.tv_nsec < 0)
      {
         wait.tv_sec--;
         wait.tv_nsec += 1000000000;
      }
      if (nanosleep(&wait, NULL) != 0 && errno != EINTR)
         return errno;
   }
}

/* Days since 1970-01-01 to a date in the proleptic Gregorian calendar (the
 * civil-from-days method: eras of 400 years, years from March) */
static void civil_from_days(int64_t days, int64_t *year, int *month, int *day)
{
   days += 719468;
   const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
   const int64_t of_era = days - era * 146097;
   const int64_t year_of_era = (of_era - of_era / 1460 + of_era / 36524 - of_era / 146096) / 365;
   const int64_t day_of_year = of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
   const int64_t month_from_march = (5 * day_of_year + 2) / 153;
   *day = (int)(day_of_year - (153 * month_from_march + 2) / 5 + 1);
   *month = (int)(month_from_march < 10 ? month_from_march + 3 : month_from_march - 9);
   *year = year_of_era + era * 400 + (*month <= 2);
}

struct tm *libre_gmtime_r(const time_t *time, struct tm *out)
{
   if (!time || !out)
   {
      errno = EINVAL;
      return NULL;
   }
   int64_t days = *time / 86400;
   int64_t seconds = *time % 86400;
   if (seconds < 0)
   {
      seconds += 86400;
      days--;
   }
   int64_t year;
   int month, day;
   civil_from_days(days, &year, &month, &day);
   if (year - 1900 > INT32_MAX || year - 1900 < INT32_MIN)
   {
      errno = EOVERFLOW;
      return NULL;
   }
   static const int before_month[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
   const int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
   memset(out, 0, sizeof *out);
   out->tm_sec = (int)(seconds % 60);
   out->tm_min = (int)(seconds / 60 % 60);
   out->tm_hour = (int)(seconds / 3600);
   out->tm_mday = day;
   out->tm_mon = month - 1;
   out->tm_year = (int)(year - 1900);
   out->tm_wday = (int)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday */
   out->tm_yday = before_month[month - 1] + day - 1 + (leap && month > 2);
   out->tm_isdst = 0;
   out->tm_gmtoff = 0;
   out->tm_zone = (char *)"UTC";
   return out;
}

/* The console's localtime, which knows its time zone, under a lock */
static pthread_mutex_t g_time_lock = PTHREAD_MUTEX_INITIALIZER;

struct tm *libre_localtime_r(const time_t *time, struct tm *out)
{
   if (!time || !out)
   {
      errno = EINVAL;
      return NULL;
   }
   pthread_mutex_lock(&g_time_lock);
   const struct tm *local = localtime(time);
   if (local)
      *out = *local;
   pthread_mutex_unlock(&g_time_lock);
   return local ? out : NULL;
}

clock_t libre_times(struct tms *out)
{
   const long ticks = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 128;
   struct rusage usage;
   if (out)
   {
      memset(out, 0, sizeof *out);
      if (getrusage(RUSAGE_SELF, &usage) == 0)
      {
         out->tms_utime = (clock_t)(usage.ru_utime.tv_sec * ticks + usage.ru_utime.tv_usec * ticks / 1000000);
         out->tms_stime = (clock_t)(usage.ru_stime.tv_sec * ticks + usage.ru_stime.tv_usec * ticks / 1000000);
      }
   }
   struct timespec now;
   if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
      return (clock_t)-1;
   return (clock_t)(now.tv_sec * ticks + now.tv_nsec / (1000000000 / ticks));
}

/* The system: what a title is on */

int libre___xuname(int size, void *buffer)
{
   if (size <= 0 || !buffer)
   {
      errno = EFAULT;
      return -1;
   }
   static const char *const fields[5] = {"FreeBSD", "ps5", "11.0-RELEASE", "PlayStation 5", "amd64"};
   char *at = buffer;
   for (int i = 0; i < 5; i++, at += size)
   {
      strncpy(at, fields[i], (size_t)size - 1);
      at[size - 1] = '\0';
   }
   return 0;
}

int libre_getpagesizes(size_t *sizes, int count)
{
   /* The console's page: 16 KiB, the one size a title is given */
   if (count < 0 || (sizes && count == 0))
   {
      errno = EINVAL;
      return -1;
   }
   if (sizes)
      sizes[0] = 0x4000;
   return 1;
}

long libre_pathconf(const char *path, int name)
{
   struct stat info;
   if (stat(path, &info) != 0)
      return -1;
   switch (name)
   {
   case _PC_LINK_MAX: return 32767;
   case _PC_NAME_MAX: return 255;
   case _PC_PATH_MAX: return 1024;
   case _PC_PIPE_BUF: return 512;
   case _PC_CHOWN_RESTRICTED: return 1;
   case _PC_NO_TRUNC: return 1;
   case _PC_FILESIZEBITS: return 64;
   case _PC_SYMLINK_MAX: return 1024;
   default: errno = EINVAL; return -1;
   }
}

char *libre_strsignal(int signal)
{
   if (signal > 0 && signal < 32 && sys_siglist[signal])
      return (char *)sys_siglist[signal];
   static char unknown[32];
   snprintf(unknown, sizeof unknown, "Unknown signal: %d", signal);
   return unknown;
}

int libre_dladdr(const void *address, Dl_info *info)
{
   /* Only the title itself, one file: /app0/eboot.bin. Mesa names its disk
    * cache by the file's time (disk_cache_get_function_identifier) */
   const char *at = address;
   if (!info || at < __libre_image_start || at >= __libre_image_end)
      return 0;
   info->dli_fname = "/app0/eboot.bin";
   info->dli_fbase = __libre_image_start;
   info->dli_sname = NULL;
   info->dli_saddr = NULL;
   return 1;
}

/* What a title cannot do: one process, no users, no terminals, no shell */

int libre_isatty(int fd)
{
   errno = fcntl(fd, F_GETFD) < 0 ? EBADF : ENOTTY;
   return 0;
}

int libre_getpwnam_r(const char *name, struct passwd *entry, char *buffer, size_t size, struct passwd **result)
{
   (void)name, (void)entry, (void)buffer, (void)size;
   *result = NULL; /* no such user */
   return 0;
}

int libre_getpwuid_r(uid_t user, struct passwd *entry, char *buffer, size_t size, struct passwd **result)
{
   (void)user, (void)entry, (void)buffer, (void)size;
   *result = NULL;
   return 0;
}

static mode_t g_umask = 022;

mode_t libre_umask(mode_t mask)
{
   /* Kept and given back; the console applies none */
   const mode_t previous = g_umask;
   g_umask = mask & 0777;
   return previous;
}

pid_t libre_fork(void)
{
   errno = ENOSYS;
   return -1;
}

pid_t libre_setsid(void)
{
   errno = EPERM;
   return -1;
}

pid_t libre_wait4(pid_t process, int *status, int options, struct rusage *usage)
{
   (void)process, (void)status, (void)options, (void)usage;
   errno = ECHILD;
   return -1;
}

int libre_fchown(int fd, uid_t user, gid_t group)
{
   (void)fd, (void)user, (void)group;
   errno = EPERM;
   return -1;
}

void *libre_sbrk(intptr_t increment)
{
   (void)increment;
   errno = ENOMEM;
   return (void *)-1;
}

long libre_syscall(long number, ...)
{
   /* write (FreeBSD's 4), which Abseil's raw logging makes directly; no other */
   if (number == 4)
   {
      va_list arguments;
      va_start(arguments, number);
      const int fd = va_arg(arguments, int);
      const void *buffer = va_arg(arguments, const void *);
      const size_t size = va_arg(arguments, size_t);
      va_end(arguments);
      return write(fd, buffer, size);
   }
   errno = ENOSYS;
   return -1;
}

int libre_posix_madvise(void *address, size_t size, int advice)
{
   (void)address, (void)size, (void)advice;
   return 0; /* advice only */
}

void libre_openlog(const char *ident, int option, int facility)
{
   (void)ident, (void)option, (void)facility; /* no system log for a title */
}

FILE *libre_popen(const char *command, const char *mode)
{
   (void)command, (void)mode;
   errno = ENOSYS;
   return NULL;
}

int libre_pclose(FILE *stream)
{
   (void)stream;
   errno = ECHILD;
   return -1;
}

int libre_memfd_create(const char *name, unsigned flags)
{
   (void)name, (void)flags;
   errno = ENOSYS;
   return -1;
}

/* Strings and sorting */

char *libre_strcasestr(const char *haystack, const char *needle)
{
   const size_t length = strlen(needle);
   if (!length)
      return (char *)haystack;
   for (; *haystack; haystack++)
   {
      if (strncasecmp(haystack, needle, length) == 0)
         return (char *)haystack;
   }
   return NULL;
}

/* assert's failure (FreeBSD's __assert): the expression and where, then the end */
void libre___assert(const char *function, const char *file, int line, const char *expression)
{
   fprintf(stderr, "Assertion failed: (%s), function %s, file %s, line %d.\n", expression, function ? function : "?",
      file, line);
   abort();
}

void *libre___memset_chk(void *destination, int value, size_t size, size_t room)
{
   if (size > room)
      abort(); /* the compiler's object-size check: an overflow */
   return memset(destination, value, size);
}

static void swap_bytes(char *a, char *b, size_t size)
{
   while (size--)
   {
      const char t = *a;
      *a++ = *b;
      *b++ = t;
   }
}

/* qsort_r in FreeBSD's order (the argument before the comparison, and first to
 * it), as the SDK's stdlib.h declares it: a heapsort, in place, no recursion */
void libre_qsort_r(void *base, size_t count, size_t size, void *argument,
   int (*compare)(void *, const void *, const void *))
{
   char *const items = base;
   if (count < 2 || !size)
      return;
   /* Sift the item at root down within the first `end` items */
   for (size_t start = count / 2; start-- > 0;)
   {
      for (size_t root = start, child; (child = 2 * root + 1) < count; root = child)
      {
         if (child + 1 < count && compare(argument, items + child * size, items + (child + 1) * size) < 0)
            child++;
         if (compare(argument, items + root * size, items + child * size) >= 0)
            break;
         swap_bytes(items + root * size, items + child * size, size);
      }
   }
   for (size_t end = count - 1; end > 0; end--)
   {
      swap_bytes(items, items + end * size, size);
      for (size_t root = 0, child; (child = 2 * root + 1) < end; root = child)
      {
         if (child + 1 < end && compare(argument, items + child * size, items + (child + 1) * size) < 0)
            child++;
         if (compare(argument, items + root * size, items + child * size) >= 0)
            break;
         swap_bytes(items + root * size, items + child * size, size);
      }
   }
}
