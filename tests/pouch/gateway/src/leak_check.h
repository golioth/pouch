/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/ztest.h>

#include "stub_blockbuf.h"
#include "wrap.h"

/** Assert that the watched pointer was freed once and that no blockbuf block is in use. */
static inline void assert_all_released(void)
{
    zassert_equal(wrap_free_count(), 1, "context not freed exactly once");
    zassert_equal(stub_blockbuf_in_use(), 0, "blocks leaked");
}
