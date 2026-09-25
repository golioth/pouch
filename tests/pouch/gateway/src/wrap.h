/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/** Count free() calls on @p ptr from now on, starting from zero. */
void wrap_free_watch(const void *ptr);

/** Number of free() calls on the pointer passed to wrap_free_watch(). */
unsigned int wrap_free_count(void);
