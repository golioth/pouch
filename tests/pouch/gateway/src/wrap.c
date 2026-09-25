/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include "wrap.h"

static const void *watched;
static unsigned int watched_frees;

void __real_free(void *ptr);

void __wrap_free(void *ptr)
{
    if (ptr != NULL && ptr == watched)
    {
        watched_frees++;
    }

    __real_free(ptr);
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
