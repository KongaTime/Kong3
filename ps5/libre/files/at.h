/*
 * RPCS3 for PS5 - the *at calls' paths, shared by the layer's file calls.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md), inside it: not for titles.
 */

#ifndef LIBRE_FILES_AT_H
#define LIBRE_FILES_AT_H

/* The path an *at call means: *path itself (absolute, or relative to the working
 * directory), or a new one joined to the directory dirfd's, set in *joined to
 * free. 0, or -1 with errno set (files/dir.c) */
__attribute__((visibility("hidden"))) int libre_resolve_at(int dirfd, const char **path, char **joined);

#endif
