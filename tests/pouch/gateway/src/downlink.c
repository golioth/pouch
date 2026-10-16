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

static void downlink_after(void *fixture)
{
    ARG_UNUSED(fixture);
    wrap_hooks_disarm();
}

ZTEST_SUITE(downlink, NULL, NULL, downlink_setup, downlink_after, NULL);

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

ZTEST(downlink, test_split_final_payload_ends_with_its_last_chunk)
{
    static uint8_t payload[MAX_PLAINTEXT_BLOCK_SIZE + 3];
    static uint8_t out[sizeof(payload)];

    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t) i;
    }

    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    zassert_ok(pouch_gateway_downlink_block_cb(payload, sizeof(payload), true, dl));
    zassert_equal(stub_blockbuf_alloc_count(), 2, "the payload was not split");

    size_t len = sizeof(out);
    bool is_last = false;
    zassert_ok(pouch_gateway_downlink_get_data(dl, out, &len, &is_last));
    zassert_equal(len, sizeof(payload), "the stream ended before the last chunk");
    zassert_mem_equal(out, payload, sizeof(payload));
    zassert_true(is_last);

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

/* The next read returns no bytes and ends the stream. */
static void assert_ends_empty(struct pouch_gateway_downlink_context *dl)
{
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last = false;

    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_equal(len, 0);
    zassert_true(is_last, "the stream did not end");
    zassert_true(pouch_gateway_downlink_is_complete(dl));
}

ZTEST(downlink, test_clean_end_without_final_block_allocates_no_block)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    pouch_gateway_downlink_end_cb(0, dl);
    zassert_equal(stub_blockbuf_alloc_count(), 0, "the end took a block");
    zassert_equal(data_available_calls, 1);
    assert_ends_empty(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_clean_end_with_the_pool_exhausted_ends_the_stream)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);
    stub_blockbuf_fail_alloc_from(1);

    pouch_gateway_downlink_end_cb(0, dl);
    zassert_equal(data_available_calls, 1, "the end did not wake the consumer");
    assert_ends_empty(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

/* An empty final response with @p data as its payload, then a clean end. */
static void end_with_an_empty_final_response(const uint8_t *data)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    zassert_ok(pouch_gateway_downlink_block_cb(data, 0, true, dl));
    pouch_gateway_downlink_end_cb(0, dl);
    zassert_equal(stub_blockbuf_alloc_count(), 0, "the empty response took a block");
    zassert_equal(data_available_calls, 1);
    assert_ends_empty(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_empty_final_response_allocates_no_block)
{
    end_with_an_empty_final_response((const uint8_t *) "");
}

ZTEST(downlink, test_empty_final_response_without_payload_allocates_no_block)
{
    end_with_an_empty_final_response(NULL);
}

ZTEST(downlink, test_clean_end_after_non_final_blocks_ends_after_the_data)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "ab", 2, false, dl));
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "cd", 2, false, dl));

    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last = true;
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_equal(len, 4);
    zassert_mem_equal(buf, "abcd", 4);
    zassert_false(is_last);
    data_available_calls = 0;

    pouch_gateway_downlink_end_cb(0, dl);
    zassert_equal(data_available_calls, 1, "the end did not wake the waiting consumer");
    assert_ends_empty(dl);

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

/*
 * Opens a downlink held by both users and consumes the notification that open() arms, so that
 * WAITING is clear and notifications are counted from zero.
 */
static struct pouch_gateway_downlink_context *open_and_consume_first_notification(void)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    uint8_t byte;
    size_t len = 1;
    bool is_last;

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "a", 1, false, dl));
    zassert_equal(data_available_calls, 1);
    /* One byte drains the block without reaching the empty queue. */
    zassert_ok(pouch_gateway_downlink_get_data(dl, &byte, &len, &is_last));
    zassert_equal(len, 1);
    data_available_calls = 0;

    return dl;
}

static void queue_block(void *arg)
{
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "b", 1, false, arg));
}

static void end_cleanly(void *arg)
{
    pouch_gateway_downlink_end_cb(0, arg);
}

static void queue_block_and_end_cleanly(void *arg)
{
    queue_block(arg);
    end_cleanly(arg);
}

ZTEST(downlink, test_block_queued_before_the_drain_waits_is_returned)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();
    uint8_t byte;
    size_t len = 1;
    bool is_last;

    wrap_msgq_get_empty_hook(1, queue_block, dl);
    zassert_ok(pouch_gateway_downlink_get_data(dl, &byte, &len, &is_last));
    zassert_false(wrap_hook_pending(), "the drain never found the queue empty");
    zassert_equal(len, 1, "block queued before WAITING was armed is neither returned nor notified");
    zassert_equal(byte, 'b');
    zassert_false(is_last);
    zassert_equal(data_available_calls, 0);

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "c", 1, true, dl));
    zassert_equal(data_available_calls, 0, "notified a consumer that is not waiting");

    len = 1;
    zassert_ok(pouch_gateway_downlink_get_data(dl, &byte, &len, &is_last));
    zassert_equal(byte, 'c');
    zassert_true(is_last);

    pouch_gateway_downlink_end_cb(0, dl);
    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_end_published_before_the_drain_waits_skips_no_block)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last;

    wrap_msgq_get_empty_hook(1, queue_block_and_end_cleanly, dl);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_false(wrap_hook_pending(), "the drain never found the queue empty");
    zassert_equal(len, 1, "the block queued before the end was skipped");
    zassert_equal(buf[0], 'b');
    zassert_true(is_last);
    zassert_equal(data_available_calls, 0);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_end_published_after_the_snapshot_is_not_lost)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last;

    wrap_atomic_get_value_hook(end_cleanly, dl);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_false(wrap_hook_pending(), "the drain took no flags snapshot");
    zassert_equal(len, 0);
    zassert_false(is_last);
    zassert_equal(data_available_calls, 1, "the end did not wake the waiting consumer");

    len = sizeof(buf);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_equal(len, 0);
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_block_and_end_published_after_the_snapshot_are_returned)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last;

    wrap_atomic_get_value_hook(queue_block_and_end_cleanly, dl);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_false(wrap_hook_pending(), "the drain took no flags snapshot");
    zassert_equal(data_available_calls, 1, "the waiting consumer was not woken exactly once");
    zassert_equal(len, 1);
    zassert_equal(buf[0], 'b');
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_end_published_after_the_recheck_skips_no_block)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last;

    wrap_msgq_get_empty_hook(2, queue_block_and_end_cleanly, dl);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_false(wrap_hook_pending(), "the drain did not recheck the queue");
    zassert_false(is_last, "the block queued before the end was skipped");
    zassert_equal(len, 0);
    zassert_equal(data_available_calls, 1, "the block did not wake the waiting consumer");

    len = sizeof(buf);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_equal(len, 1);
    zassert_equal(buf[0], 'b');
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

/* Opens a downlink held by both users. */
static struct pouch_gateway_downlink_context *open_with_producer(void)
{
    struct pouch_gateway_downlink_context *dl =
        pouch_gateway_downlink_open(data_available_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);
    pouch_gateway_downlink_acquire(dl);

    return dl;
}

/* The next read returns exactly the @p len bytes of @p data, not flagged last. */
static void assert_reads(struct pouch_gateway_downlink_context *dl, const char *data, size_t len)
{
    uint8_t buf[8];
    size_t got = len;
    bool is_last = true;

    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &got, &is_last));
    zassert_equal(got, len);
    zassert_mem_equal(buf, data, len);
    zassert_false(is_last);
}

/* The next read fails with -EIO, returns no bytes and ends the stream. */
static void assert_ends_truncated(struct pouch_gateway_downlink_context *dl)
{
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last = true;

    zassert_equal(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last),
                  -EIO,
                  "the truncation was not reported");
    zassert_equal(len, 0, "the failing read returned bytes");
    zassert_false(is_last);
    zassert_true(pouch_gateway_downlink_is_complete(dl));
}

ZTEST(downlink, test_refusal_before_any_data_reports_truncation)
{
    struct pouch_gateway_downlink_context *dl = open_with_producer();

    stub_blockbuf_fail_alloc_from(1);
    zassert_equal(pouch_gateway_downlink_block_cb((const uint8_t *) "ab", 2, true, dl), -ENOMEM);
    pouch_gateway_downlink_end_cb(-ENOMEM, dl);
    assert_ends_truncated(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_refusal_after_data_reports_truncation_despite_a_clean_end)
{
    struct pouch_gateway_downlink_context *dl = open_with_producer();

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "ab", 2, false, dl));
    stub_blockbuf_fail_alloc_from(2);
    zassert_equal(pouch_gateway_downlink_block_cb((const uint8_t *) "cd", 2, false, dl), -ENOMEM);
    stub_blockbuf_fail_alloc_from(0);
    /* A transport that ignores the refusal and still delivers the final block. */
    int err = pouch_gateway_downlink_block_cb((const uint8_t *) "ef", 2, true, dl);
    pouch_gateway_downlink_end_cb(0, dl);
    assert_reads(dl, "ab", 2);
    assert_ends_truncated(dl);
    zassert_equal(err, -ENOMEM, "the final block after the refusal was accepted");
    zassert_equal(stub_blockbuf_alloc_count(), 1, "a block was taken after the refusal");

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_full_queue_reports_truncation_despite_a_clean_end)
{
    static const char data[] = "abcdefgh";
    struct pouch_gateway_downlink_context *dl = open_with_producer();

    BUILD_ASSERT(CONFIG_POUCH_GATEWAY_NUM_BLOCKS < sizeof(data));
    for (size_t i = 0; i < CONFIG_POUCH_GATEWAY_NUM_BLOCKS; i++)
    {
        zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) &data[i], 1, false, dl));
    }

    /* The put waits for CONFIG_POUCH_GATEWAY_DOWNLINK_BLOCK_TIMEOUT, then fails. */
    zassert_equal(pouch_gateway_downlink_block_cb((const uint8_t *) "z", 1, true, dl), -ENOMEM);
    zassert_equal(stub_blockbuf_alloc_count(),
                  CONFIG_POUCH_GATEWAY_NUM_BLOCKS + 1,
                  "the block was not refused by the queue");
    pouch_gateway_downlink_end_cb(0, dl);
    assert_reads(dl, data, CONFIG_POUCH_GATEWAY_NUM_BLOCKS);
    assert_ends_truncated(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_transport_error_after_data_reports_truncation)
{
    struct pouch_gateway_downlink_context *dl = open_with_producer();

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "ab", 2, false, dl));
    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "cd", 2, false, dl));
    pouch_gateway_downlink_end_cb(-EIO, dl);
    assert_reads(dl, "abc", 3);
    /* The failing read also drops the "d" it copied. */
    assert_ends_truncated(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_transport_error_after_the_final_block_skips_no_data)
{
    struct pouch_gateway_downlink_context *dl = open_with_producer();

    zassert_ok(pouch_gateway_downlink_block_cb((const uint8_t *) "abc", 3, true, dl));
    pouch_gateway_downlink_end_cb(-EIO, dl);

    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last = false;
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_equal(len, 3);
    zassert_mem_equal(buf, "abc", 3);
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

static void end_with_error(void *arg)
{
    pouch_gateway_downlink_end_cb(-EIO, arg);
}

static void queue_block_and_end_with_error(void *arg)
{
    queue_block(arg);
    end_with_error(arg);
}

ZTEST(downlink, test_truncation_published_before_the_drain_waits_skips_no_block)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();

    wrap_msgq_get_empty_hook(1, queue_block_and_end_with_error, dl);
    assert_reads(dl, "b", 1);
    zassert_false(wrap_hook_pending(), "the drain never found the queue empty");
    assert_ends_truncated(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(downlink, test_truncation_published_after_the_snapshot_is_not_lost)
{
    struct pouch_gateway_downlink_context *dl = open_and_consume_first_notification();
    uint8_t buf[8];
    size_t len = sizeof(buf);
    bool is_last;

    wrap_atomic_get_value_hook(end_with_error, dl);
    zassert_ok(pouch_gateway_downlink_get_data(dl, buf, &len, &is_last));
    zassert_false(wrap_hook_pending(), "the drain took no flags snapshot");
    zassert_equal(len, 0);
    zassert_false(is_last);
    zassert_equal(data_available_calls, 1, "the truncation did not wake the waiting consumer");
    assert_ends_truncated(dl);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}
