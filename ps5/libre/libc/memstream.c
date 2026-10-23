/*
 * RPCS3 for PS5 - open_memstream.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds
 * open_memstream to libre_open_memstream, and PS5_Vulkan's recipe wraps fflush
 * and fclose (--wrap), which land here. Written from POSIX's open_memstream:
 * the caller's buffer and size are brought up to date at each fflush and at
 * fclose, the buffer ends with a NUL, and the caller frees it after fclose.
 *
 * The console gives a title no stream on callbacks (no funopen, fopencookie or
 * fmemopen), so the stream is a real one: the C library's, on a temporary file
 * deleted at once. Seeks and writes past the end work as on any file, the gap
 * read as zeros. fflush and fclose then copy what it holds to the caller's
 * buffer. Not on anonymous shared memory: the console's takes no write past
 * its end (EAGAIN), and a title has no /tmp or /data (build 167, on my
 * console; /download0 and /app0 take a file).
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int __real_fflush(FILE *stream);
int __real_fclose(FILE *stream);

struct memstream
{
   struct memstream *next;
   FILE *stream;
   char **buffer_out;
   size_t *size_out;
   char *buffer;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct memstream *g_streams;

/* Something to write to and read back: a temporary file, deleted at once, in
 * TMPDIR or the first of the title's folders that takes one */
static int backing(void)
{
   const char *const folders[] = {getenv("TMPDIR"), "/download0", "/app0"};
   int error = ENOENT;
   for (size_t i = 0; i < sizeof folders / sizeof folders[0]; i++)
   {
      if (!folders[i] || !*folders[i])
         continue;
      char path[256];
      snprintf(path, sizeof path, "%s/.memstream.XXXXXX", folders[i]);
      const int fd = mkstemp(path);
      if (fd >= 0)
      {
         unlink(path);
         return fd;
      }
      error = errno;
   }
   errno = error;
   return -1;
}

/* The caller's buffer and size brought up to date: all the stream holds,
 * then a NUL; the size the smaller of its length and the position */
static int publish(struct memstream *entry)
{
   const int fd = fileno(entry->stream);
   struct stat status;
   const off_t position = ftello(entry->stream);
   if (fd < 0 || position < 0 || fstat(fd, &status) != 0)
      return -1;
   const size_t length = (size_t)status.st_size;
   char *buffer = realloc(entry->buffer, length + 1);
   if (!buffer)
   {
      errno = ENOMEM;
      return -1;
   }
   entry->buffer = buffer;
   size_t done = 0;
   while (done < length)
   {
      const ssize_t got = pread(fd, buffer + done, length - done, (off_t)done);
      if (got <= 0)
      {
         if (got < 0 && errno == EINTR)
            continue;
         break;
      }
      done += (size_t)got;
   }
   memset(buffer + done, 0, length + 1 - done);
   *entry->buffer_out = buffer;
   *entry->size_out = (size_t)position < length ? (size_t)position : length;
   return 0;
}

FILE *libre_open_memstream(char **buffer, size_t *size)
{
   if (!buffer || !size)
   {
      errno = EINVAL;
      return NULL;
   }
   struct memstream *entry = calloc(1, sizeof *entry);
   if (!entry || !(entry->buffer = calloc(1, 1)))
   {
      free(entry);
      errno = ENOMEM;
      return NULL;
   }
   const int fd = backing();
   if (fd < 0 || !(entry->stream = fdopen(fd, "w+")))
   {
      const int error = fd < 0 ? errno : errno ? errno : EIO;
      if (fd >= 0)
         close(fd);
      free(entry->buffer);
      free(entry);
      errno = error;
      return NULL;
   }
   entry->buffer_out = buffer;
   entry->size_out = size;
   *buffer = entry->buffer;
   *size = 0;
   pthread_mutex_lock(&g_lock);
   entry->next = g_streams;
   g_streams = entry;
   pthread_mutex_unlock(&g_lock);
   return entry->stream;
}

int __wrap_fflush(FILE *stream)
{
   int result = __real_fflush(stream);
   pthread_mutex_lock(&g_lock);
   for (struct memstream *entry = g_streams; entry; entry = entry->next)
   {
      if ((!stream || entry->stream == stream) && publish(entry) != 0)
         result = EOF;
   }
   pthread_mutex_unlock(&g_lock);
   return result;
}

int __wrap_fclose(FILE *stream)
{
   pthread_mutex_lock(&g_lock);
   struct memstream *entry = NULL;
   for (struct memstream **link = &g_streams; *link; link = &(*link)->next)
   {
      if ((*link)->stream == stream)
      {
         entry = *link;
         *link = entry->next;
         break;
      }
   }
   pthread_mutex_unlock(&g_lock);
   if (!entry)
      return __real_fclose(stream);
   int result = __real_fflush(stream);
   if (publish(entry) != 0)
      result = EOF;
   if (__real_fclose(stream) != 0)
      result = EOF;
   free(entry);
   return result;
}
