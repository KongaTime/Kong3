/*
 * RPCS3 for PS5 - file calls a title lacks or is refused.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds access,
 * statfs, fstatfs, statvfs, fstatvfs, futimens, utimensat, link, symlink,
 * readlink, realpath, mkstemp, mkstemps and posix_fallocate to these libre_
 * functions. Written from POSIX and FreeBSD's documentation, on the calls the
 * console gives a title:
 *
 * - The file system's figures are fixed: the console refuses a title the call
 *   that gives them (below); statvfs is the same figures in POSIX's form.
 * - The console's file systems for a title have no links, and the calls that
 *   make or read them are given a title only in libkernel_sys, whose imports are
 *   null at run time: links cannot be made, and no path is one.
 * - realpath is refused to a title by the console (EPERM): here, the absolute
 *   path with "." and ".." resolved, which, with no links, is the real one.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include "at.h"

uint32_t libre_arc4random_uniform(uint32_t upper); /* libc/misc.c */

int libre_access(const char *path, int mode)
{
   struct stat info;
   if (stat(path, &info) != 0)
      return -1;
   if (mode == F_OK)
      return 0;
   /* The permission bits for anyone: a title is the files' one user */
   const mode_t bits = info.st_mode;
   if (((mode & R_OK) && !(bits & (S_IRUSR | S_IRGRP | S_IROTH))) ||
      ((mode & W_OK) && !(bits & (S_IWUSR | S_IWGRP | S_IWOTH))) ||
      ((mode & X_OK) && !(bits & (S_IXUSR | S_IXGRP | S_IXOTH))))
   {
      errno = EACCES;
      return -1;
   }
   return 0;
}

/* The file system's figures. The console refuses a title _fstatfs (FreeBSD's
 * fstatfs, which libkernel exports under that name): EPERM on /app0, its files
 * and folders (build 164, on my console). So the figures are fixed: 64 KiB
 * blocks, /app0's own block size; 1 TiB, 512 GiB of it free, more than any PS3
 * game asks for; mounted from /app0, not a device (RPCS3 looks for disc drives
 * among devices). The path or descriptor itself is checked */
#define BLOCK 0x10000ull
#define TOTAL_BLOCKS ((1ull << 40) / BLOCK)
#define FREE_BLOCKS ((1ull << 39) / BLOCK)

static void figures(struct statfs *info)
{
   memset(info, 0, sizeof *info);
   info->f_version = STATFS_VERSION;
   info->f_flags = MNT_LOCAL | MNT_NOSUID;
   info->f_bsize = BLOCK;
   info->f_iosize = BLOCK;
   info->f_blocks = TOTAL_BLOCKS;
   info->f_bfree = FREE_BLOCKS;
   info->f_bavail = (int64_t)FREE_BLOCKS;
   info->f_files = 1ull << 32;
   info->f_ffree = (int64_t)(1ull << 31);
   info->f_namemax = 255;
   strcpy(info->f_fstypename, "ps5");
   strcpy(info->f_mntfromname, "/app0");
   strcpy(info->f_mntonname, "/app0");
}

int libre_fstatfs(int fd, struct statfs *info)
{
   struct stat file;
   if (fstat(fd, &file) != 0)
      return -1;
   figures(info);
   return 0;
}

int libre_statfs(const char *path, struct statfs *info)
{
   struct stat file;
   if (stat(path, &file) != 0)
      return -1;
   figures(info);
   return 0;
}

static void to_statvfs(const struct statfs *info, struct statvfs *out)
{
   memset(out, 0, sizeof *out);
   out->f_frsize = info->f_bsize;  /* the unit the counts are in */
   out->f_bsize = info->f_iosize;  /* the best size to transfer */
   out->f_blocks = info->f_blocks;
   out->f_bfree = info->f_bfree;
   out->f_bavail = info->f_bavail > 0 ? (fsblkcnt_t)info->f_bavail : 0;
   out->f_files = info->f_files;
   out->f_ffree = info->f_ffree > 0 ? (fsfilcnt_t)info->f_ffree : 0;
   out->f_favail = out->f_ffree;
   out->f_namemax = info->f_namemax;
   out->f_flag = ((info->f_flags & MNT_RDONLY) ? ST_RDONLY : 0) | ((info->f_flags & MNT_NOSUID) ? ST_NOSUID : 0);
}

int libre_fstatvfs(int fd, struct statvfs *out)
{
   struct statfs info;
   if (libre_fstatfs(fd, &info) != 0)
      return -1;
   to_statvfs(&info, out);
   return 0;
}

int libre_statvfs(const char *path, struct statvfs *out)
{
   struct statfs info;
   if (libre_statfs(path, &info) != 0)
      return -1;
   to_statvfs(&info, out);
   return 0;
}

/* A timespec's time for utimes: now, the file's own (omitted), or the time given */
static int to_timeval(const struct timespec *time, const struct timespec *own, struct timeval *out)
{
   struct timespec at = *time;
   if (time->tv_nsec == UTIME_NOW)
   {
      if (clock_gettime(CLOCK_REALTIME, &at) != 0)
         return -1;
   }
   else if (time->tv_nsec == UTIME_OMIT)
      at = *own;
   else if (time->tv_nsec < 0 || time->tv_nsec >= 1000000000)
   {
      errno = EINVAL;
      return -1;
   }
   out->tv_sec = at.tv_sec;
   out->tv_usec = at.tv_nsec / 1000;
   return 0;
}

static int times_for(const struct timespec times[2], const struct stat *info, struct timeval out[2])
{
   return to_timeval(&times[0], &info->st_atim, &out[0]) != 0 || to_timeval(&times[1], &info->st_mtim, &out[1]) != 0
      ? -1
      : 0;
}

int libre_futimens(int fd, const struct timespec times[2])
{
   if (!times)
      return futimes(fd, NULL);
   struct stat info;
   struct timeval set[2];
   if (fstat(fd, &info) != 0 || times_for(times, &info, set) != 0)
      return -1;
   return futimes(fd, set);
}

int libre_utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
   (void)flags; /* no links: following them or not is the same */
   char *joined;
   if (libre_resolve_at(dirfd, &path, &joined) != 0)
      return -1;
   int result;
   if (!times)
      result = utimes(path, NULL);
   else
   {
      struct stat info;
      struct timeval set[2];
      result = stat(path, &info) != 0 || times_for(times, &info, set) != 0 ? -1 : utimes(path, set);
   }
   const int error = errno;
   free(joined);
   errno = error;
   return result;
}

int libre_link(const char *from, const char *to)
{
   (void)from, (void)to;
   errno = EPERM; /* the file system makes no links */
   return -1;
}

int libre_symlink(const char *target, const char *path)
{
   (void)target, (void)path;
   errno = EPERM;
   return -1;
}

ssize_t libre_readlink(const char *path, char *buffer, size_t size)
{
   (void)buffer, (void)size;
   struct stat info;
   if (lstat(path, &info) != 0)
      return -1;
   errno = EINVAL; /* there, but not a link */
   return -1;
}

char *libre_realpath(const char *path, char *resolved)
{
   if (!path)
   {
      errno = EINVAL;
      return NULL;
   }
   if (!*path)
   {
      errno = ENOENT;
      return NULL;
   }
   char whole[PATH_MAX * 2];
   if (path[0] == '/')
      whole[0] = '\0';
   else if (!getcwd(whole, PATH_MAX))
      return NULL;
   if (strlen(whole) + 1 + strlen(path) + 1 > sizeof whole)
   {
      errno = ENAMETOOLONG;
      return NULL;
   }
   strcat(whole, "/");
   strcat(whole, path);

   /* The components, "." left out and ".." taking the last one back */
   char out[PATH_MAX];
   size_t length = 0;
   for (const char *at = whole; *at;)
   {
      while (*at == '/')
         at++;
      const char *end = at;
      while (*end && *end != '/')
         end++;
      const size_t part = (size_t)(end - at);
      if (part == 0 || (part == 1 && at[0] == '.'))
      {
      }
      else if (part == 2 && at[0] == '.' && at[1] == '.')
      {
         while (length > 0 && out[length - 1] != '/')
            length--;
         if (length > 0)
            length--;
      }
      else
      {
         if (length + 1 + part + 1 > sizeof out)
         {
            errno = ENAMETOOLONG;
            return NULL;
         }
         out[length++] = '/';
         memcpy(out + length, at, part);
         length += part;
      }
      at = end;
   }
   if (!length)
      out[length++] = '/';
   out[length] = '\0';

   /* It must be there */
   struct stat info;
   if (stat(out, &info) != 0)
      return NULL;
   if (!resolved && !(resolved = malloc(PATH_MAX)))
   {
      errno = ENOMEM;
      return NULL;
   }
   memcpy(resolved, out, length + 1);
   return resolved;
}

int libre_mkstemps(char *path, int suffix)
{
   static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
   const size_t length = path ? strlen(path) : 0;
   if (suffix < 0 || (size_t)suffix > length)
   {
      errno = EINVAL;
      return -1;
   }
   /* The X's before the suffix, at least six */
   char *const end = path + length - suffix;
   char *start = end;
   while (start > path && start[-1] == 'X')
      start--;
   if (end - start < 6)
   {
      errno = EINVAL;
      return -1;
   }
   for (int tries = 0; tries < 1000; tries++)
   {
      for (char *at = start; at < end; at++)
         *at = letters[libre_arc4random_uniform(sizeof letters - 1)];
      const int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
      if (fd >= 0 || errno != EEXIST)
         return fd;
   }
   errno = EEXIST;
   return -1;
}

int libre_mkstemp(char *path)
{
   return libre_mkstemps(path, 0);
}

int libre_posix_fallocate(int fd, off_t offset, off_t size)
{
   /* The file made long enough; the console reserves its blocks as it writes */
   if (offset < 0 || size <= 0)
      return EINVAL;
   struct stat info;
   if (fstat(fd, &info) != 0)
      return errno;
   if (!S_ISREG(info.st_mode))
      return ENODEV;
   if (info.st_size >= offset + size)
      return 0;
   return ftruncate(fd, offset + size) == 0 ? 0 : errno;
}
