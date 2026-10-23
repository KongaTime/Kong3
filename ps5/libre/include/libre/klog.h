/*
 * RPCS3 for PS5 - the kernel log.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md).
 */

#ifndef LIBRE_KLOG_H
#define LIBRE_KLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Standard error to the kernel log from now on, each line after the prefix
 * (copied; at most 63 bytes kept). 0, or -1 and errno */
int libre_klog_capture_stderr(const char *prefix);

#ifdef __cplusplus
}
#endif

#endif
