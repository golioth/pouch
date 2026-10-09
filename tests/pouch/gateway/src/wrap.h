/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/** Count free() calls on @p ptr from now on, starting from zero. */
void wrap_free_watch(const void *ptr);

/** Number of free() calls on the pointer passed to wrap_free_watch(). */
unsigned int wrap_free_count(void);

/**
 * Run @p hook with @p arg once, after the @p nth (from 1) pouch_msgq_get() from now on that
 * finds its queue empty.
 *
 * Assumes the drain's 1st empty read comes before it arms WAITING, its 2nd after the snapshot.
 */
void wrap_msgq_get_empty_hook(unsigned int nth, void (*hook)(void *), void *arg);

/**
 * Run @p hook with @p arg once, after the next pouch_atomic_get_value().
 *
 * Assumes the drain's flags snapshot is its only pouch_atomic_get_value() call.
 */
void wrap_atomic_get_value_hook(void (*hook)(void *), void *arg);

/** Disarm both hooks. */
void wrap_hooks_disarm(void);

/** True while a hook is armed and has not run. */
bool wrap_hook_pending(void);
