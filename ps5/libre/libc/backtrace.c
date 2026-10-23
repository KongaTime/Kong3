/*
 * RPCS3 for PS5 - backtraces.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds
 * backtrace and backtrace_symbols_fd to these libre_ ones. The console gives a
 * title neither. Written from FreeBSD's backtrace(3) on the unwinder the title
 * links (LLVM's libunwind, from the unwind tables the libre linker script
 * bounds), so a frame without a frame pointer is walked too:
 *
 * - backtrace: the return addresses of the caller and its callers.
 * - backtrace_symbols_fd: one line each, the address, the function and offset
 *   when dladdr knows them, and the image. To standard error it writes through
 *   stderr, which the title sends to klog (libre/klog.h); descriptor 2 itself
 *   reaches no log on the console.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <unwind.h>

struct walk
{
   void **frames;
   size_t size;
   size_t count;
   int skip;
};

static _Unwind_Reason_Code step(struct _Unwind_Context *context, void *argument)
{
   struct walk *walk = argument;
   const uintptr_t address = _Unwind_GetIP(context);
   if (!address)
      return _URC_END_OF_STACK;
   if (walk->skip > 0)
   {
      walk->skip--;
      return _URC_NO_REASON;
   }
   if (walk->count == walk->size)
      return _URC_END_OF_STACK;
   walk->frames[walk->count++] = (void *)address;
   return _URC_NO_REASON;
}

size_t libre_backtrace(void **frames, size_t size)
{
   if (!frames || !size)
      return 0;
   /* Not this function's own frame */
   struct walk walk = {frames, size, 0, 1};
   _Unwind_Backtrace(step, &walk);
   return walk.count;
}

int libre_backtrace_symbols_fd(void *const *frames, size_t count, int fd)
{
   for (size_t i = 0; i < count; i++)
   {
      char line[512];
      Dl_info info;
      memset(&info, 0, sizeof info);
      int length;
      if (dladdr(frames[i], &info) && info.dli_fname)
      {
         if (info.dli_sname)
            length = snprintf(line, sizeof line, "%p <%s+%zu> at %s\n", frames[i], info.dli_sname,
               (size_t)((uintptr_t)frames[i] - (uintptr_t)info.dli_saddr), info.dli_fname);
         else
            length = snprintf(line, sizeof line, "%p <%s+0x%zx>\n", frames[i], info.dli_fname,
               (size_t)((uintptr_t)frames[i] - (uintptr_t)info.dli_fbase));
      }
      else
      {
         length = snprintf(line, sizeof line, "%p\n", frames[i]);
      }
      if (length < 0)
         return -1;
      const size_t size = (size_t)length < sizeof line ? (size_t)length : sizeof line - 1;
      if (fd == STDERR_FILENO)
      {
         if (fwrite(line, 1, size, stderr) != size)
            return -1;
      }
      else if (write(fd, line, size) != (ssize_t)size)
      {
         return -1;
      }
   }
   if (fd == STDERR_FILENO)
      fflush(stderr);
   return 0;
}
