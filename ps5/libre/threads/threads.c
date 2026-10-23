/*
 * RPCS3 for PS5 - the title's threads (libre/threads.h).
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md), written from what the title
 * needs of its threads (PS5_RPCS3's Utilities/Thread.cpp, the Vulkan driver's
 * and libc++'s threads) and the console's direct-memory calls.
 *
 * A stack is direct memory mapped in a place of its own, 64 KiB above the start
 * of the place, which stays reserved and unmapped: a guard an overrun faults on.
 * Every thread starts in start_thread, which runs the thread's function and then
 * its thread_local destructors. A joined thread's stack goes back at once; a
 * detached thread cannot give back the stack it is still running on, so it goes
 * back when a later create, join or detach finds the thread ended over a second
 * before.
 */

#include "libre/threads.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t size, size_t alignment,
   int memory_type, int64_t *start);
extern int sceKernelReleaseDirectMemory(int64_t start, size_t size);
extern size_t sceKernelGetDirectMemorySize(void);
extern int sceKernelMapDirectMemory(void **address, size_t size, int protection, int flags, int64_t start,
   size_t alignment);
extern int sceKernelReserveVirtualRange(void **address, size_t size, int flags, size_t alignment);
extern int sceKernelMunmap(void *address, size_t size);

extern int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg);
extern int __real_pthread_join(pthread_t thread, void **value);
extern int __real_pthread_detach(pthread_t thread);

#define MAP_AT 0x10
#define READ_WRITE 0x3
#define MEMORY_WRITE_BACK 0
#define UNIT 0x10000u
#define GUARD UNIT
#define STACK_LEAST ((size_t)2 << 20)
/* Stacks go from here up: above the shared-memory views' start, below RPCS3's
 * own search for ranges (0x40_0000_0000) */
#define STACK_AREA 0x3800000000ull
/* A detached thread's stack goes back this long after the thread ended */
#define LINGER_NS 1000000000ull

struct stack
{
   void *place; /* the whole place: the guard, then the stack */
   size_t place_size;
   int64_t direct;
};

struct thread
{
   void *(*start)(void *);
   void *arg;
   pthread_t id;
   struct stack stack; /* place 0: the thread brought its own */
   int detached;
   int ended;
   uint64_t ended_at;
   struct thread *next;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct thread *g_running; /* not yet joined, or detached and not yet ended */
static struct thread *g_ended;   /* detached and ended: their stacks to give back */

static uint64_t now_ns(void)
{
   struct timespec at;
   clock_gettime(CLOCK_MONOTONIC, &at);
   return (uint64_t)at.tv_sec * 1000000000ull + (uint64_t)at.tv_nsec;
}

static int stack_make(size_t size, struct stack *stack)
{
   size = (size + UNIT - 1) & ~(size_t)(UNIT - 1);
   const size_t place_size = size + GUARD;
   void *place = (void *)STACK_AREA;
   if (sceKernelReserveVirtualRange(&place, place_size, 0, UNIT) != 0)
      return ENOMEM;
   if ((uint64_t)place < STACK_AREA)
   {
      (void)sceKernelMunmap(place, place_size);
      return ENOMEM;
   }
   int64_t direct = 0;
   if (sceKernelAllocateDirectMemory(0, (int64_t)sceKernelGetDirectMemorySize(), size, UNIT, MEMORY_WRITE_BACK, &direct) != 0)
   {
      (void)sceKernelMunmap(place, place_size);
      return ENOMEM;
   }
   void *at = (char *)place + GUARD;
   if (sceKernelMapDirectMemory(&at, size, READ_WRITE, MAP_AT, direct, 0) != 0 || at != (char *)place + GUARD)
   {
      (void)sceKernelMunmap(place, place_size);
      (void)sceKernelReleaseDirectMemory(direct, size);
      return ENOMEM;
   }
   stack->place = place;
   stack->place_size = place_size;
   stack->direct = direct;
   return 0;
}

static void stack_give_back(struct stack *stack)
{
   if (!stack->place)
      return;
   (void)sceKernelMunmap(stack->place, stack->place_size);
   (void)sceKernelReleaseDirectMemory(stack->direct, stack->place_size - GUARD);
   stack->place = NULL;
}

/* Gives back the stacks of detached threads ended long enough ago; returns the
 * list of their records, to be freed outside the lock. Under g_lock */
static struct thread *take_ended(void)
{
   const uint64_t now = now_ns();
   struct thread *done = NULL;
   for (struct thread **at = &g_ended; *at;)
   {
      struct thread *thread = *at;
      if (now - thread->ended_at >= LINGER_NS)
      {
         *at = thread->next;
         thread->next = done;
         done = thread;
      }
      else
         at = &thread->next;
   }
   return done;
}

static void give_back(struct thread *done)
{
   while (done)
   {
      struct thread *next = done->next;
      stack_give_back(&done->stack);
      free(done);
      done = next;
   }
}

static void reap(void)
{
   pthread_mutex_lock(&g_lock);
   struct thread *done = take_ended();
   pthread_mutex_unlock(&g_lock);
   give_back(done);
}

/* Takes a thread's record out of the running list. Under g_lock */
static struct thread *take_running(struct thread *thread)
{
   for (struct thread **at = &g_running; *at; at = &(*at)->next)
   {
      if (*at == thread)
      {
         *at = thread->next;
         thread->next = NULL;
         return thread;
      }
   }
   return NULL;
}

static struct thread *find_running(pthread_t id)
{
   for (struct thread *thread = g_running; thread; thread = thread->next)
   {
      if (thread->id && pthread_equal(thread->id, id))
         return thread;
   }
   return NULL;
}

/* The thread has ended (its function returned, or it called libre_pthread_exit) */
static void thread_ended(struct thread *thread)
{
   pthread_mutex_lock(&g_lock);
   thread->ended = 1;
   thread->ended_at = now_ns();
   if (thread->detached && take_running(thread))
   {
      thread->next = g_ended;
      g_ended = thread;
   }
   pthread_mutex_unlock(&g_lock);
}

static void *start_thread(void *argument)
{
   struct thread *thread = argument;
   void *const value = thread->start(thread->arg);
   libre_run_thread_destructors();
   thread_ended(thread);
   return value;
}

int __wrap_pthread_create(pthread_t *id, const pthread_attr_t *attr, void *(*start)(void *), void *arg)
{
   reap();

   struct thread *thread = calloc(1, sizeof *thread);
   if (!thread)
      return EAGAIN;
   thread->start = start;
   thread->arg = arg;

   /* The caller's stack, or one here of at least 2 MiB */
   void *own_stack = NULL;
   size_t size = 0;
   int detach = PTHREAD_CREATE_JOINABLE;
   if (attr)
   {
      (void)pthread_attr_getstackaddr(attr, &own_stack);
      (void)pthread_attr_getstacksize(attr, &size);
      (void)pthread_attr_getdetachstate(attr, &detach);
   }
   thread->detached = detach == PTHREAD_CREATE_DETACHED;

   pthread_attr_t made;
   const pthread_attr_t *use = attr;
   if (!own_stack)
   {
      if (stack_make(size > STACK_LEAST ? size : STACK_LEAST, &thread->stack) != 0)
      {
         free(thread);
         return EAGAIN;
      }
      /* The caller's settings, with the stack */
      if (pthread_attr_init(&made) != 0)
      {
         stack_give_back(&thread->stack);
         free(thread);
         return EAGAIN;
      }
      if (attr)
      {
         int value = 0;
         struct sched_param param;
         if (pthread_attr_getinheritsched(attr, &value) == 0)
            (void)pthread_attr_setinheritsched(&made, value);
         if (pthread_attr_getschedpolicy(attr, &value) == 0)
            (void)pthread_attr_setschedpolicy(&made, value);
         if (pthread_attr_getschedparam(attr, &param) == 0)
            (void)pthread_attr_setschedparam(&made, &param);
         if (pthread_attr_getscope(attr, &value) == 0)
            (void)pthread_attr_setscope(&made, value);
      }
      (void)pthread_attr_setdetachstate(&made, detach);
      (void)pthread_attr_setstack(&made, (char *)thread->stack.place + GUARD, thread->stack.place_size - GUARD);
      use = &made;
   }

   pthread_mutex_lock(&g_lock);
   thread->next = g_running;
   g_running = thread;
   pthread_mutex_unlock(&g_lock);

   pthread_t created;
   const int result = __real_pthread_create(&created, use, start_thread, thread);
   if (use == &made)
      pthread_attr_destroy(&made);

   pthread_mutex_lock(&g_lock);
   if (result != 0)
      (void)take_running(thread);
   else
      thread->id = created;
   pthread_mutex_unlock(&g_lock);

   if (result != 0)
   {
      stack_give_back(&thread->stack);
      free(thread);
      return result;
   }
   *id = created;
   return 0;
}

int __wrap_pthread_join(pthread_t id, void **value)
{
   const int result = __real_pthread_join(id, value);
   if (result == 0)
   {
      /* Gone for good: its stack goes back now */
      pthread_mutex_lock(&g_lock);
      struct thread *thread = find_running(id);
      if (thread)
         (void)take_running(thread);
      pthread_mutex_unlock(&g_lock);
      if (thread)
      {
         stack_give_back(&thread->stack);
         free(thread);
      }
   }
   reap();
   return result;
}

int __wrap_pthread_detach(pthread_t id)
{
   const int result = __real_pthread_detach(id);
   if (result == 0)
   {
      pthread_mutex_lock(&g_lock);
      struct thread *thread = find_running(id);
      if (thread)
      {
         thread->detached = 1;
         if (thread->ended)
         {
            (void)take_running(thread);
            thread->next = g_ended;
            g_ended = thread;
         }
      }
      pthread_mutex_unlock(&g_lock);
   }
   reap();
   return result;
}

void libre_pthread_exit(void *value)
{
   libre_run_thread_destructors();
   pthread_mutex_lock(&g_lock);
   struct thread *thread = find_running(pthread_self());
   pthread_mutex_unlock(&g_lock);
   if (thread)
      thread_ended(thread);
   pthread_exit(value);
}

/* The thread_local destructors: each thread's list, newest first, under a key
 * (not thread-local storage, which is what they must run before) */
struct destructor
{
   void (*run)(void *);
   void *object;
   struct destructor *next;
};

static pthread_key_t g_destructors;
static pthread_once_t g_destructors_once = PTHREAD_ONCE_INIT;

static void run_list(void *list)
{
   /* A thread that never came through start_thread or libre_pthread_exit: the
    * key's own destructor runs them, as late as libkernel runs it */
   (void)pthread_setspecific(g_destructors, list);
   libre_run_thread_destructors();
}

static void make_key(void)
{
   (void)pthread_key_create(&g_destructors, run_list);
}

int libre___cxa_thread_atexit_impl(void (*destructor)(void *), void *object, void *dso)
{
   (void)dso;
   pthread_once(&g_destructors_once, make_key);
   struct destructor *entry = malloc(sizeof *entry);
   if (!entry)
      return -1;
   entry->run = destructor;
   entry->object = object;
   entry->next = pthread_getspecific(g_destructors);
   return pthread_setspecific(g_destructors, entry) == 0 ? 0 : (free(entry), -1);
}

void libre_run_thread_destructors(void)
{
   pthread_once(&g_destructors_once, make_key);
   /* A destructor may register more: run until none is left */
   for (struct destructor *entry; (entry = pthread_getspecific(g_destructors));)
   {
      (void)pthread_setspecific(g_destructors, entry->next);
      entry->run(entry->object);
      free(entry);
   }
}

/* A thread's CPUs, through the console's 64-bit masks */
extern int32_t scePthreadGetaffinity(pthread_t thread, uint64_t *mask);
extern int32_t scePthreadSetaffinity(pthread_t thread, uint64_t mask);

/* The console's error codes are 0x80020000 and the errno value */
static int error_number(int32_t result)
{
   if (!result)
      return 0;
   const uint32_t code = (uint32_t)result;
   return code >= 0x80020001u && code < 0x80020100u ? (int)(code & 0xff) : EINVAL;
}

int libre_pthread_setaffinity_np(pthread_t thread, size_t size, const void *set)
{
   if (!set)
      return EINVAL;
   const unsigned char *bytes = set;
   uint64_t mask = 0;
   for (size_t i = 0; i < size; i++)
   {
      if (i < sizeof mask)
         mask |= (uint64_t)bytes[i] << (8 * i);
      else if (bytes[i])
         return EINVAL; /* a CPU the console does not have */
   }
   if (!mask)
      return EINVAL;
   return error_number(scePthreadSetaffinity(thread, mask));
}

int libre_pthread_getaffinity_np(pthread_t thread, size_t size, void *set)
{
   if (!set)
      return EINVAL;
   uint64_t mask = 0;
   const int error = error_number(scePthreadGetaffinity(thread, &mask));
   if (error)
      return error;
   memset(set, 0, size);
   memcpy(set, &mask, size < sizeof mask ? size : sizeof mask);
   return 0;
}
