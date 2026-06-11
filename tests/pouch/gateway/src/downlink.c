/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests for the gateway downlink module (src/gateway/downlink.c).
 *
 * The downlink module receives data chunks from the cloud (via
 * pouch_gateway_downlink_block_cb) and lets a consumer drain them
 * via pouch_gateway_downlink_get_data.  It also signals completion
 * via the data_available callback.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "gateway/downlink.h"

/* Number of times the data_available callback has fired. */
static int data_available_calls;

static void data_available_cb(void *arg)
{
    ARG_UNUSED(arg);
    data_available_calls++;
}

static void downlink_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    data_available_calls = 0;
}

ZTEST_SUITE(downlink, NULL, NULL, downlink_setup, NULL, NULL);

ZTEST(downlink, test_open_close)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    zassert_false(pouch_gateway_downlink_is_complete(dl));

    pouch_gateway_downlink_close(dl);
}

ZTEST(downlink, test_get_data_empty_returns_no_bytes_and_arms_callback)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);

    uint8_t buf[32];
    size_t dst_len = sizeof(buf);
    bool is_last = true;

    int err = pouch_gateway_downlink_get_data(dl, buf, &dst_len, &is_last);
    zassert_ok(err);
    zassert_equal(dst_len, 0);
    zassert_false(is_last);

    /* Pushing a block now should fire the data_available callback. */
    const uint8_t chunk[] = {'h', 'i'};
    err = pouch_gateway_downlink_block_cb(chunk, sizeof(chunk), false, dl);
    zassert_ok(err);
    zassert_equal(data_available_calls, 1);

    pouch_gateway_downlink_close(dl);
}

ZTEST(downlink, test_single_block_round_trip)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);

    const uint8_t chunk[] = {'p', 'o', 'u', 'c', 'h'};
    int err = pouch_gateway_downlink_block_cb(chunk, sizeof(chunk), true, dl);
    zassert_ok(err);

    /* Drain in one shot. */
    uint8_t buf[8] = {0};
    size_t dst_len = sizeof(buf);
    bool is_last = false;
    err = pouch_gateway_downlink_get_data(dl, buf, &dst_len, &is_last);
    zassert_ok(err);
    zassert_equal(dst_len, sizeof(chunk));
    zassert_mem_equal(buf, chunk, sizeof(chunk));
    zassert_true(is_last);
    zassert_true(pouch_gateway_downlink_is_complete(dl));

    pouch_gateway_downlink_close(dl);
}

ZTEST(downlink, test_multi_block_concatenation_and_is_last)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);

    const uint8_t a[] = {'a', 'b', 'c'};
    const uint8_t b[] = {'d', 'e', 'f', 'g'};
    const uint8_t c[] = {'h', 'i'};

    zassert_ok(pouch_gateway_downlink_block_cb(a, sizeof(a), false, dl));
    zassert_ok(pouch_gateway_downlink_block_cb(b, sizeof(b), false, dl));
    zassert_ok(pouch_gateway_downlink_block_cb(c, sizeof(c), true, dl));

    /* Drain a few bytes at a time to exercise the cross-block path. */
    uint8_t buf[16] = {0};
    size_t total = 0;
    bool is_last = false;

    while (!is_last)
    {
        size_t want = 4; /* less than any single block */
        int err = pouch_gateway_downlink_get_data(dl, buf + total, &want, &is_last);
        zassert_ok(err);
        zassert_true(want > 0 || is_last);
        total += want;
        if (total >= sizeof(buf))
        {
            break;
        }
    }

    zassert_equal(total, sizeof(a) + sizeof(b) + sizeof(c));
    zassert_mem_equal(buf, "abcdefghi", 9);
    zassert_true(pouch_gateway_downlink_is_complete(dl));

    pouch_gateway_downlink_close(dl);
}

ZTEST(downlink, test_block_cb_rejected_after_abort)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);

    /* Push one block, then abort.  Abort marks the downlink as
     * aborted; subsequent block_cb calls must be rejected.
     */
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "x", 1, false, dl));

    pouch_gateway_downlink_abort(dl);

    int err = pouch_gateway_downlink_block_cb((const uint8_t *) "y", 1, false, dl);
    zassert_equal(err, -ECANCELED);

    pouch_gateway_downlink_close(dl);
}

ZTEST(downlink, test_end_cb_failure_makes_drain_complete_with_is_last)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);

    /* Simulate a cloud-side error before any block was queued. */
    pouch_gateway_downlink_end_cb(-EIO, dl);

    /* Should now have fired data_available so the consumer can drain. */
    zassert_equal(data_available_calls, 1);

    uint8_t buf[8];
    size_t dst_len = sizeof(buf);
    bool is_last = false;
    int err = pouch_gateway_downlink_get_data(dl, buf, &dst_len, &is_last);
    zassert_ok(err);
    zassert_equal(dst_len, 0);
    zassert_true(is_last);
    zassert_true(pouch_gateway_downlink_is_complete(dl));

    pouch_gateway_downlink_close(dl);
}
