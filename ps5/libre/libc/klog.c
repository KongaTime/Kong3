/*
 * RPCS3 for PS5 - standard error to the kernel log.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md; libre/klog.h). A title's
 * standard error goes nowhere, and the kernel log (klog) is what can be read
 * of a run. stderr (the C library's __stderrp, which a title may set) becomes a
 * stream on a pipe, and a thread reads the pipe and hands the kernel each line
 * after the prefix, through libkernel's sceKernelDebugOutText, which reaches
 * /dev/klog on any channel (build 167, on my console). A line longer than the
 * kernel takes at once goes in parts, each after the prefix. What is still in
 * the pipe when the title is killed is lost.
 *
 * Descriptor 2 is the console's deci_stderr device, whose writes reach no log
 * a title can read, and dup2 onto it did not take (build 166): writes to the
 * descriptor itself, not through stderr, are caught only where it does.
 */

#include "libre/klog.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int sceKernelDebugOutText(int channel, const char *text);

#define PART 960 /* the most of a line in one message */

static char g_prefix[64];

static void emit(const char *text, size_t length)
{
   char message[sizeof g_prefix + PART + 2];
   snprintf(message, sizeof message, "%s%.*s\n", g_prefix, (int)length, text);
   sceKernelDebugOutText(0, message);
}

static void *reader(void *argument)
{
   const int fd = (int)(intptr_t)argument;
   char line[PART];
   size_t used = 0;
   for (;;)
   {
      const ssize_t got = read(fd, line + used, sizeof line - used);
      if (got <= 0)
      {
         if (got < 0 && errno == EINTR)
            continue;
         break;
      }
      used += (size_t)got;
      /* Each whole line, then the part of a line that fills the buffer */
      size_t start = 0;
      for (size_t i = 0; i < used; i++)
      {
         if (line[i] == '\n')
         {
            emit(line + start, i - start);
            start = i + 1;
         }
      }
      if (start == 0 && used == sizeof line)
      {
         emit(line, used);
         start = used;
      }
      memmove(line, line + start, used - start);
      used -= start;
   }
   if (used)
      emit(line, used);
   close(fd);
   return NULL;
}

int libre_klog_capture_stderr(const char *prefix)
{
   snprintf(g_prefix, sizeof g_prefix, "%s", prefix ? prefix : "");
   int ends[2];
   if (pipe(ends) != 0)
      return -1;
   /* The reader first: standard error never points at a pipe nobody reads */
   pthread_attr_t attributes;
   pthread_attr_init(&attributes);
   pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
   pthread_t thread;
   const int error = pthread_create(&thread, &attributes, reader, (void *)(intptr_t)ends[0]);
   pthread_attr_destroy(&attributes);
   if (error)
   {
      close(ends[0]);
      close(ends[1]);
      errno = error;
      return -1;
   }
   /* stderr a stream on the pipe, unbuffered as stderr is; and descriptor 2 the
    * pipe too where the console lets it be (it did not, build 166) */
   FILE *stream = fdopen(ends[1], "w");
   if (!stream)
   {
      const int fdopen_error = errno;
      close(ends[1]); /* the reader sees the end and stops */
      errno = fdopen_error;
      return -1;
   }
   setvbuf(stream, NULL, _IONBF, 0);
   fflush(stderr);
   dup2(ends[1], STDERR_FILENO);
   stderr = stream;
   return 0;
}
