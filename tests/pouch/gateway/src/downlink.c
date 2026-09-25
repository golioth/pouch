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

#include "block.h"
#include "gateway/downlink.h"

#include "leak_check.h"
#include "stub_blockbuf.h"
#include "wrap.h"

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
    stub_blockbuf_reset_counters();
}

ZTEST_SUITE(downlink, NULL, NULL, downlink_setup, NULL, NULL);

ZTEST(downlink, test_open_close)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    zassert_false(pouch_gateway_downlink_is_complete(dl));

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_get_data_empty_returns_no_bytes_and_arms_callback)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

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

    pouch_gateway_downlink_end_cb(0, dl);
    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_single_block_round_trip)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

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

    pouch_gateway_downlink_end_cb(0, dl);
    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_multi_block_concatenation_and_is_last)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

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

    pouch_gateway_downlink_end_cb(0, dl);
    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_block_cb_rejected_after_close)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    /* Push one block, then close; subsequent block_cb calls must be rejected. */
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "x", 1, false, dl));

    pouch_gateway_downlink_close(dl);

    int err = pouch_gateway_downlink_block_cb((const uint8_t *) "y", 1, false, dl);
    zassert_equal(err, -ECANCELED);

    pouch_gateway_downlink_end_cb(-ECANCELED, dl);
    assert_all_released();
}

/* The callback argument points at the context the notification closes. */
static void close_from_notification_cb(void *arg)
{
    struct pouch_gateway_downlink_context **dl = arg;

    data_available_calls++;
    pouch_gateway_downlink_close(*dl);
}

ZTEST(downlink, test_close_from_notification_stops_a_split_payload_mid_callback)
{
    /* Two full chunks, so the loop has one left when the notification for
     * the first one closes the downlink.
     */
    static const uint8_t payload[2 * MAX_PLAINTEXT_BLOCK_SIZE];
    struct pouch_gateway_downlink_context *dl;

    dl = pouch_gateway_downlink_open(close_from_notification_cb, &dl);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    int err = pouch_gateway_downlink_block_cb(payload, sizeof(payload), true, dl);
    zassert_equal(data_available_calls, 1, "the close has to land inside the loop");
    zassert_equal(stub_blockbuf_alloc_count(), 1, "a chunk was allocated after the close");
    zassert_equal(err, -ECANCELED);

    /* As the cloud transport does after a failed block_cb(). */
    pouch_gateway_downlink_end_cb(-ECANCELED, dl);
    assert_all_released();
}

ZTEST(downlink, test_end_cb_failure_makes_drain_complete_with_is_last)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

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
    assert_all_released();
}

ZTEST(downlink, test_close_before_end_keeps_context_until_end)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    /* Leave one block partly drained and one queued. */
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "abc", 3, false, dl));
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "de", 2, false, dl));

    uint8_t byte;
    size_t len = 1;
    bool is_last;
    zassert_ok(pouch_gateway_downlink_get_data(dl, &byte, &len, &is_last));
    zassert_equal(len, 1);

    pouch_gateway_downlink_close(dl);
    zassert_equal(wrap_free_count(), 0, "freed while the producer holds a reference");
    zassert_equal(pouch_gateway_downlink_block_cb((const uint8_t *) "f", 1, false, dl), -ECANCELED);

    pouch_gateway_downlink_end_cb(-ECANCELED, dl);
    assert_all_released();
}

ZTEST(downlink, test_end_before_close_keeps_context_until_close)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "abc", 3, true, dl));
    pouch_gateway_downlink_end_cb(0, dl);
    zassert_equal(wrap_free_count(), 0, "freed while the consumer holds a reference");

    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last = false;
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_equal(len, 3);
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

/* The callback argument points at the context; drains it to the end and closes it. */
static void drain_and_close_cb(void *arg)
{
    struct pouch_gateway_downlink_context **dl = arg;
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last = false;

    data_available_calls++;
    zassert_ok(pouch_gateway_downlink_get_data(*dl, buf, &len, &is_last));
    zassert_true(is_last);
    pouch_gateway_downlink_close(*dl);
    zassert_equal(wrap_free_count(), 0, "freed under the running end_cb()");
}

ZTEST(downlink, test_close_from_end_notification_keeps_context_until_end_returns)
{
    struct pouch_gateway_downlink_context *dl;

    dl = pouch_gateway_downlink_open(drain_and_close_cb, &dl);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    pouch_gateway_downlink_end_cb(-EIO, dl);
    zassert_equal(data_available_calls, 1);
    assert_all_released();
}
