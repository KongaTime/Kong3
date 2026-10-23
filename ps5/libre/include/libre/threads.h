/*
 * RPCS3 for PS5 - the title's threads: their stacks, and their ends.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md). The link wraps
 * pthread_create, pthread_join and pthread_detach (threads/threads.c): a thread
 * that brings no stack of its own gets one of at least 2 MiB in direct memory,
 * with a guard below it, in place of the console's small default from the
 * title's flexible memory; joining or detaching the thread gives it back.
 *
 * A thread's C++ thread_local destructors (registered through
 * __cxa_thread_atexit_impl, which the console's C library lacks and the link
 * binds here) run when its function returns, before libkernel's own key
 * destructors free its thread-local storage, which the console's compiler
 * emulates: run after, they used freed memory.
 */

#ifndef LIBRE_THREADS_H
#define LIBRE_THREADS_H

#ifdef __cplusplus
extern "C" {
#endif

/* pthread_exit, after the thread's thread_local destructors */
__attribute__((noreturn)) void libre_pthread_exit(void *value);

/* Registers a thread_local destructor (the C++ runtime's call) */
int libre___cxa_thread_atexit_impl(void (*destructor)(void *), void *object, void *dso);

/* Runs this thread's thread_local destructors now, newest first */
void libre_run_thread_destructors(void);

#ifdef __cplusplus
}
#endif

#endif
