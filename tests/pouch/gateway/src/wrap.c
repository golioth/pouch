/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <pouch/port.h>

#include "wrap.h"

static const void *watched;
static unsigned int watched_frees;

static void (*empty_get_hook)(void *);
static void *empty_get_arg;
static unsigned int empty_gets_left;

static void (*get_value_hook)(void *);
static void *get_value_arg;

void __real_free(void *ptr);
int __real_pouch_msgq_get(pouch_msgq_t *msgq, void *buf, pouch_timeout_t timeout);
long __real_pouch_atomic_get_value(const pouch_atomic_t *target);

void __wrap_free(void *ptr)
{
    if (ptr != NULL && ptr == watched)
    {
        watched_frees++;
    }

    __real_free(ptr);
}

int __wrap_pouch_msgq_get(pouch_msgq_t *msgq, void *buf, pouch_timeout_t timeout)
{
    int err = __real_pouch_msgq_get(msgq, buf, timeout);

    if (err != 0 && empty_get_hook != NULL && --empty_gets_left == 0)
    {
        void (*hook)(void *) = empty_get_hook;

        empty_get_hook = NULL;
        hook(empty_get_arg);
    }

    return err;
}

long __wrap_pouch_atomic_get_value(const pouch_atomic_t *target)
{
    long value = __real_pouch_atomic_get_value(target);

    if (get_value_hook != NULL)
    {
        void (*hook)(void *) = get_value_hook;

        get_value_hook = NULL;
        hook(get_value_arg);
    }

    return value;
}

void wrap_free_watch(const void *ptr)
{
    watched = ptr;
    watched_frees = 0;
}

unsigned int wrap_free_count(void)
{
    return watched_frees;
}

void wrap_msgq_get_empty_hook(unsigned int nth, void (*hook)(void *), void *arg)
{
    empty_get_hook = hook;
    empty_get_arg = arg;
    empty_gets_left = nth;
}

void wrap_atomic_get_value_hook(void (*hook)(void *), void *arg)
{
    get_value_hook = hook;
    get_value_arg = arg;
}

void wrap_hooks_disarm(void)
{
    empty_get_hook = NULL;
    get_value_hook = NULL;
}

bool wrap_hook_pending(void)
{
    return empty_get_hook != NULL || get_value_hook != NULL;
}
