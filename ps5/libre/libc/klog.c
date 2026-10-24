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
 * /dev/klog on any channel (build 167, on my console), from a queue in memory
 * (below). A line longer than the kernel takes at once goes in parts, each
 * after the prefix. What is still in the pipe or the queue when the title is
 * killed is lost.
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int sceKernelDebugOutText(int channel, const char *text);

#define PART 960 /* the most of a line in one message */
#define QUEUE (8u << 20) /* what may wait for the kernel, beyond the pipe */

static char g_prefix[64];

/* sceKernelDebugOutText held the thread that called it for 2.7 s now and
 * then (build 177: once in 3000 lines at 200 a second, on my console). Called
 * from the pipe's reader, that filled the pipe, and every thread writing to
 * standard error then waited in turn: RPCS3, logging there from each thread,
 * stood still for 2.7 s every 3 to 4 s in a game that logged 130 warnings a
 * second (Army of Two: The 40th Day, build 176). So one thread empties the pipe
 * into a queue in memory at once, and another hands the queue to the kernel;
 * what does not fit in the queue is dropped, and counted, rather than waited for */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static char *g_queue;
static size_t g_head, g_used; /* the oldest byte, and how many wait */
static size_t g_dropped;      /* bytes dropped since the last note */
static int g_ended;

static void emit(const char *text, size_t length)
{
   char message[sizeof g_prefix + PART + 2];
   snprintf(message, sizeof message, "%s%.*s\n", g_prefix, (int)length, text);
   sceKernelDebugOutText(0, message);
}

/* The pipe to the queue, never waiting on the kernel */
static void *reader(void *argument)
{
   const int fd = (int)(intptr_t)argument;
   char chunk[16384];
   for (;;)
   {
      const ssize_t got = read(fd, chunk, sizeof chunk);
      if (got <= 0)
      {
         if (got < 0 && errno == EINTR)
            continue;
         break;
      }
      pthread_mutex_lock(&g_lock);
      if (QUEUE - g_used < (size_t)got)
         g_dropped += (size_t)got;
      else
      {
         for (ssize_t i = 0; i < got;)
         {
            const size_t tail = (g_head + g_used) % QUEUE;
            size_t part = QUEUE - tail;
            if (part > (size_t)(got - i))
               part = (size_t)(got - i);
            memcpy(g_queue + tail, chunk + i, part);
            g_used += part;
            i += (ssize_t)part;
         }
      }
      pthread_cond_signal(&g_ready);
      pthread_mutex_unlock(&g_lock);
   }
   pthread_mutex_lock(&g_lock);
   g_ended = 1;
   pthread_cond_signal(&g_ready);
   pthread_mutex_unlock(&g_lock);
   close(fd);
   return NULL;
}

/* The queue to the kernel, a line at a time */
static void *writer(void *argument)
{
   (void)argument;
   char line[PART];
   size_t used = 0;
   for (;;)
   {
      pthread_mutex_lock(&g_lock);
      while (!g_used && !g_ended && !g_dropped)
         pthread_cond_wait(&g_ready, &g_lock);
      const size_t dropped = g_dropped;
      g_dropped = 0;
      size_t got = sizeof line - used;
      if (got > g_used)
         got = g_used;
      if (got > QUEUE - g_head)
         got = QUEUE - g_head;
      memcpy(line + used, g_queue + g_head, got);
      g_head = (g_head + got) % QUEUE;
      g_used -= got;
      const int ended = g_ended && !g_used;
      pthread_mutex_unlock(&g_lock);

      if (dropped)
      {
         char note[96];
         snprintf(note, sizeof note, "(klog: %zu bytes of standard error dropped, the kernel being slow)", dropped);
         emit(note, strlen(note));
      }
      used += got;
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
      if (ended)
         break;
   }
   if (used)
      emit(line, used);
   return NULL;
}

static int start_thread(void *(*body)(void *), void *argument)
{
   pthread_attr_t attributes;
   pthread_attr_init(&attributes);
   pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
   pthread_t thread;
   const int error = pthread_create(&thread, &attributes, body, argument);
   pthread_attr_destroy(&attributes);
   return error;
}

int libre_klog_capture_stderr(const char *prefix)
{
   snprintf(g_prefix, sizeof g_prefix, "%s", prefix ? prefix : "");
   int ends[2];
   if (pipe(ends) != 0)
      return -1;
   /* The queue and both threads first: standard error never points at a pipe
    * nobody reads */
   g_queue = malloc(QUEUE);
   int error = g_queue ? start_thread(writer, NULL) : ENOMEM;
   if (!error)
      error = start_thread(reader, (void *)(intptr_t)ends[0]);
   if (error)
   {
      /* A writer already waiting stays so, harmless */
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
