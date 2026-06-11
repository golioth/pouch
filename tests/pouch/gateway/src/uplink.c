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

/*--------------------------------------------------
 * Stub cloud transport
 *------------------------------------------------*/

#define MAX_FORWARDED 4096

static struct
{
    int forward_calls;
    int forward_rc;
    uint8_t payload[MAX_FORWARDED];
    size_t payload_len;
    pouch_gateway_cloud_block2_cb_t resp_cb;
    void *resp_arg;
} stub;

static int stub_forward(const uint8_t *data,
                        size_t len,
                        pouch_gateway_cloud_block2_cb_t resp_cb,
                        void *arg)
{
    stub.forward_calls++;

    if (len <= sizeof(stub.payload))
    {
        memcpy(stub.payload, data, len);
        stub.payload_len = len;
    }

    stub.resp_cb = resp_cb;
    stub.resp_arg = arg;

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
    zassert_ok(pouch_gateway_uplink_write(up, chunk, sizeof(chunk), true));

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
    zassert_ok(pouch_gateway_uplink_write(up, a, 3, false));
    zassert_ok(pouch_gateway_uplink_write(up, b, 3, false));
    zassert_ok(pouch_gateway_uplink_write(up, c, 4, true));

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
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "x", 1, true));
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

    struct pouch_gateway_uplink *up = pouch_gateway_uplink_open(dl, on_end, NULL);
    zassert_not_null(up);
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "p", 1, true));
    pouch_gateway_uplink_close(up);

    /* The uplink should hand the stub a non-NULL resp_cb tied to the
     * downlink context.
     */
    zassert_equal(stub.forward_calls, 1);
    zassert_not_null(stub.resp_cb);
    zassert_equal_ptr(stub.resp_arg, dl);

    /* Push a synthetic response block through the wire to mimic a
     * cloud Block2 response.  This must complete the downlink.
     */
    int err = stub.resp_cb((const uint8_t *) "RESP", 4, true, stub.resp_arg);
    zassert_ok(err);

    /* Drain the downlink and verify "RESP". */
    uint8_t out[8] = {0};
    size_t out_len = sizeof(out);
    bool is_last = false;
    zassert_ok(pouch_gateway_downlink_get_data(dl, out, &out_len, &is_last));
    zassert_equal(out_len, 4);
    zassert_mem_equal(out, "RESP", 4);
    zassert_true(is_last);

    pouch_gateway_downlink_close(dl);
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
    zassert_ok(pouch_gateway_uplink_write(up, (const uint8_t *) "z", 1, true));
    pouch_gateway_uplink_close(up);

    /* No transport -> cloud_forward returns -ENODEV -> uplink reports
     * an ERROR_CLOUD.  This codifies current behaviour; if we later
     * change it to silently succeed, update this test.
     */
    zassert_equal(end_cb_calls, 1);
    zassert_equal(end_cb_result, POUCH_GATEWAY_UPLINK_ERROR_CLOUD);
}
