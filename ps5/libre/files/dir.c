/*
 * RPCS3 for PS5 - directories and the *at file calls.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); the link binds opendir,
 * fdopendir, readdir, rewinddir, dirfd, closedir, openat, fstatat, unlinkat,
 * mkdirat, renameat and fchmodat to these libre_ functions (link-title.sh).
 *
 * The console gives a title the *at calls only in libkernel_sys, whose imports
 * are null at run time, and no dirfd at all. So:
 *
 * - A directory stream is the layer's own: a descriptor opened with O_DIRECTORY,
 *   read with libkernel's getdents into a buffer whose records are the SDK's
 *   struct dirent (FreeBSD 11's, which the kernel writes), handed out in place.
 * - The *at calls are the path calls (open, stat, unlink...) on the directory's
 *   path joined to the name. The layer remembers the path of each directory
 *   descriptor it opens (opendir, openat), with the directory's device and file
 *   number, so a descriptor closed and reused for something else is not taken
 *   for it. A name that is absolute, or relative to the working directory
 *   (AT_FDCWD), goes to the path call as it is.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Directory descriptors the layer remembers paths for: those below this */
#define REMEMBERED 4096
/* getdents' buffer: the directory's block size in whole 16 KiB pages, 64 KiB at
 * least (8 KiB, smaller than a page, was refused: EINVAL, build 162) */
#define PAGE 0x4000
#define LEAST_BUFFER 0x10000

static struct
{
   char *path;
   dev_t device;
   ino_t file;
} g_paths[REMEMBERED];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

struct libre_dir
{
   int fd;
   int at, filled, size;
   char *buffer;
};

/* Remembers path for fd when fd is a directory */
static void remember(int fd, const char *path)
{
   struct stat info;
   if (fd < 0 || fd >= REMEMBERED || fstat(fd, &info) != 0 || !S_ISDIR(info.st_mode))
      return;
   char *copy = strdup(path);
   if (!copy)
      return;
   pthread_mutex_lock(&g_lock);
   free(g_paths[fd].path);
   g_paths[fd].path = copy;
   g_paths[fd].device = info.st_dev;
   g_paths[fd].file = info.st_ino;
   pthread_mutex_unlock(&g_lock);
}

/* The path fd was opened with, if it is still that directory: a copy, or NULL */
static char *path_of(int fd)
{
   struct stat info;
   if (fd < 0 || fd >= REMEMBERED || fstat(fd, &info) != 0)
      return NULL;
   char *copy = NULL;
   pthread_mutex_lock(&g_lock);
   if (g_paths[fd].path && g_paths[fd].device == info.st_dev && g_paths[fd].file == info.st_ino)
      copy = strdup(g_paths[fd].path);
   pthread_mutex_unlock(&g_lock);
   return copy;
}

/* The path an *at call means: *path itself (absolute, or relative to the working
 * directory), or a new one joined to the directory's, set in *joined to free */
static int resolve(int dirfd, const char **path, char **joined)
{
   *joined = NULL;
   if (!*path)
   {
      errno = EFAULT;
      return -1;
   }
   if ((*path)[0] == '/' || dirfd == AT_FDCWD)
      return 0;
   char *directory = path_of(dirfd);
   if (!directory)
   {
      struct stat info;
      errno = fstat(dirfd, &info) != 0 ? EBADF : S_ISDIR(info.st_mode) ? EBADF : ENOTDIR;
      return -1;
   }
   const size_t length = strlen(directory);
   const int slash = length && directory[length - 1] == '/';
   char *whole = malloc(length + !slash + strlen(*path) + 1);
   if (!whole)
   {
      free(directory);
      errno = ENOMEM;
      return -1;
   }
   strcpy(whole, directory);
   if (!slash)
      strcat(whole, "/");
   strcat(whole, *path);
   free(directory);
   *joined = whole;
   *path = whole;
   return 0;
}

DIR *libre_fdopendir(int fd)
{
   struct stat info;
   if (fstat(fd, &info) != 0)
      return NULL;
   if (!S_ISDIR(info.st_mode))
   {
      errno = ENOTDIR;
      return NULL;
   }
   size_t size = info.st_blksize > LEAST_BUFFER ? (size_t)info.st_blksize : LEAST_BUFFER;
   size = (size + PAGE - 1) & ~(size_t)(PAGE - 1);
   struct libre_dir *dir = malloc(sizeof *dir);
   char *buffer = malloc(size);
   if (!dir || !buffer)
   {
      free(dir);
      free(buffer);
      errno = ENOMEM;
      return NULL;
   }
   dir->fd = fd;
   dir->at = dir->filled = 0;
   dir->size = (int)size;
   dir->buffer = buffer;
   return (DIR *)dir;
}

DIR *libre_opendir(const char *path)
{
   const int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
   if (fd < 0)
      return NULL;
   remember(fd, path);
   DIR *dir = libre_fdopendir(fd);
   if (!dir)
   {
      const int error = errno;
      close(fd);
      errno = error;
   }
   return dir;
}

struct dirent *libre_readdir(DIR *stream)
{
   struct libre_dir *dir = (struct libre_dir *)stream;
   for (;;)
   {
      if (dir->at >= dir->filled)
      {
         const int read = getdents(dir->fd, dir->buffer, dir->size);
         if (read <= 0)
            return NULL; /* the end (errno as it was), or an error (errno set) */
         dir->at = 0;
         dir->filled = read;
      }
      struct dirent *entry = (struct dirent *)(dir->buffer + dir->at);
      if (entry->d_reclen == 0)
      {
         dir->at = dir->filled;
         return NULL;
      }
      dir->at += entry->d_reclen;
      if (entry->d_fileno)
         return entry; /* 0: a removed entry, passed over */
   }
}

void libre_rewinddir(DIR *stream)
{
   struct libre_dir *dir = (struct libre_dir *)stream;
   (void)lseek(dir->fd, 0, SEEK_SET);
   dir->at = dir->filled = 0;
}

int libre_dirfd(DIR *stream)
{
   return ((struct libre_dir *)stream)->fd;
}

int libre_closedir(DIR *stream)
{
   struct libre_dir *dir = (struct libre_dir *)stream;
   const int result = close(dir->fd);
   free(dir->buffer);
   free(dir);
   return result;
}

int libre_openat(int dirfd, const char *path, int flags, ...)
{
   int mode = 0;
   if (flags & O_CREAT)
   {
      va_list arguments;
      va_start(arguments, flags);
      mode = va_arg(arguments, int);
      va_end(arguments);
   }
   char *joined;
   if (resolve(dirfd, &path, &joined) != 0)
      return -1;
   const int fd = open(path, flags, mode);
   if (fd >= 0)
      remember(fd, path);
   const int error = errno;
   free(joined);
   errno = error;
   return fd;
}

int libre_fstatat(int dirfd, const char *path, struct stat *info, int flags)
{
   char *joined;
   if (resolve(dirfd, &path, &joined) != 0)
      return -1;
   const int result = (flags & AT_SYMLINK_NOFOLLOW) ? lstat(path, info) : stat(path, info);
   const int error = errno;
   free(joined);
   errno = error;
   return result;
}

int libre_unlinkat(int dirfd, const char *path, int flags)
{
   char *joined;
   if (resolve(dirfd, &path, &joined) != 0)
      return -1;
   const int result = (flags & AT_REMOVEDIR) ? rmdir(path) : unlink(path);
   const int error = errno;
   free(joined);
   errno = error;
   return result;
}

int libre_mkdirat(int dirfd, const char *path, mode_t mode)
{
   char *joined;
   if (resolve(dirfd, &path, &joined) != 0)
      return -1;
   const int result = mkdir(path, mode);
   const int error = errno;
   free(joined);
   errno = error;
   return result;
}

int libre_renameat(int from_dirfd, const char *from, int to_dirfd, const char *to)
{
   char *from_joined, *to_joined;
   if (resolve(from_dirfd, &from, &from_joined) != 0)
      return -1;
   if (resolve(to_dirfd, &to, &to_joined) != 0)
   {
      free(from_joined);
      return -1;
   }
   const int result = rename(from, to);
   const int error = errno;
   free(from_joined);
   free(to_joined);
   errno = error;
   return result;
}

int libre_fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
   /* No lchmod for a title: a link's own mode cannot be changed */
   if (flags & AT_SYMLINK_NOFOLLOW)
   {
      errno = ENOTSUP;
      return -1;
   }
   char *joined;
   if (resolve(dirfd, &path, &joined) != 0)
      return -1;
   const int result = chmod(path, mode);
   const int error = errno;
   free(joined);
   errno = error;
   return result;
}
