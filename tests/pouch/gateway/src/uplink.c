/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Tests for the gateway uplink module (src/gateway/uplink.c).
 *
 * The uplink module buffers data written by the broker side and
 * forwards the concatenated payload to the cloud transport (via
 * pouch_gateway_cloud_forward_pouch) when close() is called.  It
 * notifies the caller via an end_cb describing whether the forward
 * succeeded.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <pouch/gateway/cloud.h>
#include "gateway/downlink.h"
#include <pouch/gateway/uplink.h>

#include "leak_check.h"
#include "stub_blockbuf.h"
#include "wrap.h"

/*--------------------------------------------------
 * Stub cloud transport
 *------------------------------------------------*/

#define MAX_FORWARDED 4096
#define STUB_CHUNK_SIZE 128

static struct
{
    int forward_calls;
    int forward_rc;
    uint8_t payload[MAX_FORWARDED];
    size_t payload_len;
    pouch_gateway_cloud_block2_cb_t resp_cb;
    void *resp_arg;
    /* Delivered as a single Block2 response block when not NULL, flagged last unless partial. */
    const uint8_t *response;
    size_t response_len;
    bool response_partial;
} stub;

static int stub_forward(pouch_gateway_cloud_upload_chunk_cb_t chunk_cb,
                        void *chunk_arg,
                        pouch_gateway_cloud_block2_cb_t resp_cb,
                        void *resp_arg)
{
    stub.forward_calls++;
    stub.resp_cb = resp_cb;
    stub.resp_arg = resp_arg;
    stub.payload_len = 0;

    /* Drain the chunk source into a linear buffer so the tests can
     * inspect what the uplink actually produced. Cap each pull at
     * STUB_CHUNK_SIZE to exercise multi-chunk streaming.
     */
    if (chunk_cb != NULL)
    {
        while (stub.payload_len < sizeof(stub.payload))
        {
            size_t space = MIN(STUB_CHUNK_SIZE, sizeof(stub.payload) - stub.payload_len);
            size_t chunk_len = 0;
            bool is_last = false;

            int err =
                chunk_cb(stub.payload + stub.payload_len, space, &chunk_len, &is_last, chunk_arg);
            if (err)
            {
                return err;
            }
            stub.payload_len += chunk_len;
            if (is_last)
            {
                break;
            }
        }
    }

    if (stub.response != NULL)
    {
        int err = resp_cb(stub.response, stub.response_len, !stub.response_partial, resp_arg);
        if (err)
        {
            return err;
        }
    }

    return stub.forward_rc;
}

static const struct pouch_gateway_cloud_transport stub_transport = {
    .forward_pouch = stub_forward,
};

/*--------------------------------------------------
 * End-cb capture
 *------------------------------------------------*/

static int end_cb_calls;
static enum pouch_gateway_uplink_result end_cb_result;

static void on_end(void *arg, enum pouch_gateway_uplink_result res)
{
    ARG_UNUSED(arg);
    end_cb_calls++;
    end_cb_result = res;
}

/*--------------------------------------------------
 * Suite
 *------------------------------------------------*/

static void uplink_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&stub, 0, sizeof(stub));
    end_cb_calls = 0;
    end_cb_result = -1;
    stub_blockbuf_reset_counters();
    pouch_gateway_cloud_transport_register(&stub_transport);
}

static void uplink_teardown(void *fixture)
{
    ARG_UNUSED(fixture);
    pouch_gateway_cloud_transport_register(NULL);
}

ZTEST_SUITE(uplink, NULL, NULL, uplink_setup, uplink_teardown, NULL);

ZTEST(uplink, test_open_close_with_no_data)
{
    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(NULL, on_end, NULL);
    zassert_not_null(up);

    pouch_gateway_uplink_close(up);

    zassert_equal(end_cb_calls, 1);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_SUCCESS);
    zassert_equal(stub.forward_calls, 0, "Empty uplink should not call forward");
}

ZTEST(uplink, test_write_then_close_forwards_payload)
{
    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(NULL, on_end, NULL);
    zassert_not_null(up);

    const uint8_t chunk[] = {'h', 'e', 'l', 'l', 'o'};
    zassert_ok(pouch_gateway_uplink_write(up, chunk, sizeof(chunk)));

    pouch_gateway_uplink_close(up);

    zassert_equal(end_cb_calls, 1);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_SUCCESS);
    zassert_equal(stub.forward_calls, 1);
    zassert_equal(stub.payload_len, sizeof(chunk));
    zassert_mem_equal(stub.payload, chunk, sizeof(chunk));
}

ZTEST(uplink, test_multiple_writes_concatenate)
{
    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(NULL, on_end, NULL);
    zassert_not_null(up);

    const uint8_t a[] = "abc";
    const uint8_t b[] = "def";
    const uint8_t c[] = "ghij";
    zassert_ok(pouch_gateway_uplink_write(up, a, 3));
    zassert_ok(pouch_gateway_uplink_write(up, b, 3));
    zassert_ok(pouch_gateway_uplink_write(up, c, 4));

    pouch_gateway_uplink_close(up);

    zassert_equal(stub.forward_calls, 1);
    zassert_equal(stub.payload_len, 10);
    zassert_mem_equal(stub.payload, "abcdefghij", 10);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_SUCCESS);
}

ZTEST(uplink, test_forward_error_propagates_to_end_cb)
{
    stub.forward_rc = -EIO;

    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(NULL, on_end, NULL);
    zassert_not_null(up);
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "x", 1));
    pouch_gateway_uplink_close(up);

    zassert_equal(stub.forward_calls, 1);
    zassert_equal(end_cb_calls, 1);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_ERROR_CLOUD);
}

static int dl_armed_calls;
static void dl_armed_cb(void *arg)
{
    ARG_UNUSED(arg);
    dl_armed_calls++;
}

ZTEST(uplink, test_forwards_to_downlink_via_resp_cb)
{
    dl_armed_calls = 0;

    struct pouch_gateway_downlink_context *dl = pouch_gateway_downlink_open(dl_armed_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);

    stub.response = (const uint8_t *) "RESP";
    stub.response_len = 4;

    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(dl, on_end, NULL);
    zassert_not_null(up);
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "p", 1));
    pouch_gateway_uplink_close(up);

    /* The uplink should hand the stub a non-NULL resp_cb tied to the
     * downlink context.
     */
    zassert_equal(stub.forward_calls, 1);
    zassert_not_null(stub.resp_cb);
    zassert_equal_ptr(stub.resp_arg, dl);

    /* Drain the downlink and verify "RESP". */
    uint8_t out[8] = {0};
    size_t out_len = sizeof(out);
    bool is_last = false;
    zassert_ok(pouch_gateway_downlink_get_data(dl, out, &out_len, &is_last));
    zassert_equal(out_len, 4);
    zassert_mem_equal(out, "RESP", 4);
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(uplink, test_forward_error_after_part_of_the_response_truncates_the_downlink)
{
    struct pouch_gateway_downlink_context *dl = pouch_gateway_downlink_open(dl_armed_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);

    stub.response = (const uint8_t *) "RE";
    stub.response_len = 2;
    stub.response_partial = true;
    stub.forward_rc = -ETIMEDOUT;

    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(dl, on_end, NULL);
    zassert_not_null(up);
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "p", 1));
    pouch_gateway_uplink_close(up);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_ERROR_CLOUD);

    uint8_t out[8];
    size_t out_len = 2;
    bool is_last = true;
    zassert_ok(pouch_gateway_downlink_get_data(dl, out, &out_len, &is_last));
    zassert_equal(out_len, 2);
    zassert_mem_equal(out, "RE", 2);
    zassert_false(is_last);

    out_len = sizeof(out);
    zassert_equal(pouch_gateway_downlink_get_data(dl, out, &out_len, &is_last),
                  -EIO,
                  "the truncation was not reported");
    zassert_equal(out_len, 0);
    zassert_false(is_last);
    zassert_true(pouch_gateway_downlink_is_complete(dl));

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(uplink, test_failed_open_does_not_acquire_downlink)
{
    struct pouch_gateway_downlink_context *dl = pouch_gateway_downlink_open(dl_armed_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);

    stub_blockbuf_fail_alloc_from(1);
    zassert_is_null(pouch_gateway_uplink_open(dl, on_end, NULL));

    pouch_gateway_downlink_close(dl);
    assert_all_released();
}

ZTEST(uplink, test_closed_downlink_lives_until_uplink_ends)
{
    dl_armed_calls = 0;

    struct pouch_gateway_downlink_context *dl = pouch_gateway_downlink_open(dl_armed_cb, NULL);
    zassert_not_null(dl);
    wrap_free_watch(dl);

    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(dl, on_end, NULL);
    zassert_not_null(up);

    pouch_gateway_downlink_close(dl);
    zassert_equal(wrap_free_count(), 0, "freed while the uplink holds a reference");

    /* An empty uplink forwards nothing and ends the downlink with status 0. */
    pouch_gateway_uplink_close(up);
    zassert_equal(end_cb_calls, 1);
    zassert_equal(dl_armed_calls, 0, "the end notified a closed downlink");
    assert_all_released();
}

ZTEST(uplink, test_streaming_across_many_writes)
{
    /* Push enough bytes to span several block-pool blocks and
     * multiple stub streaming chunks so we exercise both the
     * queue-drain path and the last-chunk short-copy path.
     */
    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(NULL, on_end, NULL);
    zassert_not_null(up);

    const size_t chunks = 20;
    const size_t chunk_len = 200;
    uint8_t chunk[chunk_len];

    for (size_t i = 0; i < chunks; i++)
    {
        memset(chunk, (int) ('A' + (i % 26)), sizeof(chunk));
        zassert_ok(pouch_gateway_uplink_write(up, chunk, sizeof(chunk)));
    }

    pouch_gateway_uplink_close(up);

    zassert_equal(stub.forward_calls, 1);
    zassert_equal(stub.payload_len, chunks * chunk_len);

    for (size_t i = 0; i < chunks; i++)
    {
        for (size_t j = 0; j < chunk_len; j++)
        {
            zassert_equal(stub.payload[i * chunk_len + j], (uint8_t) ('A' + (i % 26)));
        }
    }
}

ZTEST(uplink, test_cloud_disabled_at_kconfig_skips_forward_locally)
{
    /* This test pretends the gateway has CONFIG_POUCH_GATEWAY_CLOUD=n
     * by unregistering the transport before closing.  In that case
     * the uplink should still end successfully but with no forward.
     */
    pouch_gateway_cloud_transport_register(NULL);

    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(NULL, on_end, NULL);
    zassert_not_null(up);
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "z", 1));
    pouch_gateway_uplink_close(up);

    /* No transport -> cloud_forward returns -ENODEV -> uplink reports
     * an ERROR_CLOUD.  This codifies current behaviour; if we later
     * change it to silently succeed, update this test.
     */
    zassert_equal(end_cb_calls, 1);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_ERROR_CLOUD);
}
