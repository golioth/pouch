/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the gateway cloud transport dispatcher
 * (src/gateway/cloud.c).
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <pouch/gateway/cloud.h>

/*--------------------------------------------------
 * Stub cloud transport that records every call.
 *------------------------------------------------*/

struct stub_state
{
    int ensure_calls;
    int forward_calls;
    int cert_calls;

    int ensure_rc;
    int forward_rc;
    int cert_rc;

    /* Last forward_pouch arguments. */
    uint8_t last_payload[256];
    size_t last_payload_len;
    pouch_gateway_cloud_block2_cb_t last_resp_cb;
    void *last_arg;

    /* Last upload_device_cert arguments. */
    uint8_t last_cert[256];
    size_t last_cert_len;
};

static struct stub_state stub;

static int stub_ensure(void)
{
    stub.ensure_calls++;
    return stub.ensure_rc;
}

static int stub_forward(const uint8_t *data,
                        size_t len,
                        pouch_gateway_cloud_block2_cb_t resp_cb,
                        void *arg)
{
    stub.forward_calls++;
    if (len <= sizeof(stub.last_payload))
    {
        memcpy(stub.last_payload, data, len);
    }
    stub.last_payload_len = len;
    stub.last_resp_cb = resp_cb;
    stub.last_arg = arg;
    return stub.forward_rc;
}

static int stub_upload_cert(const uint8_t *cert, size_t len)
{
    stub.cert_calls++;
    if (len <= sizeof(stub.last_cert))
    {
        memcpy(stub.last_cert, cert, len);
    }
    stub.last_cert_len = len;
    return stub.cert_rc;
}

static const struct pouch_gateway_cloud_transport stub_transport = {
    .ensure_ready = stub_ensure,
    .forward_pouch = stub_forward,
    .upload_device_cert = stub_upload_cert,
};

/*--------------------------------------------------
 * Tests
 *------------------------------------------------*/

static void cloud_setup(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&stub, 0, sizeof(stub));
    pouch_gateway_cloud_transport_register(NULL);
}

ZTEST_SUITE(cloud, NULL, NULL, cloud_setup, NULL, NULL);

ZTEST(cloud, test_dispatch_with_no_transport_returns_enodev)
{
    zassert_equal(pouch_gateway_cloud_ensure_ready(),
                  0,
                  "ensure_ready is optional and must succeed when not registered");

    zassert_equal(pouch_gateway_cloud_forward_pouch((const uint8_t *) "x", 1, NULL, NULL), -ENODEV);

    zassert_equal(pouch_gateway_cloud_upload_device_cert((const uint8_t *) "x", 1), -ENODEV);
}

ZTEST(cloud, test_register_and_get)
{
    zassert_is_null(pouch_gateway_cloud_transport_get());

    pouch_gateway_cloud_transport_register(&stub_transport);
    zassert_equal_ptr(pouch_gateway_cloud_transport_get(), &stub_transport);

    pouch_gateway_cloud_transport_register(NULL);
    zassert_is_null(pouch_gateway_cloud_transport_get());
}

ZTEST(cloud, test_dispatch_invokes_stub)
{
    pouch_gateway_cloud_transport_register(&stub_transport);

    /* ensure_ready */
    stub.ensure_rc = 0;
    zassert_equal(pouch_gateway_cloud_ensure_ready(), 0);
    zassert_equal(stub.ensure_calls, 1);

    /* forward_pouch */
    const uint8_t payload[] = {'a', 'b', 'c'};
    int marker = 42;
    pouch_gateway_cloud_block2_cb_t cb_marker =
        (pouch_gateway_cloud_block2_cb_t) (uintptr_t) 0xfeed;

    stub.forward_rc = 7;
    zassert_equal(pouch_gateway_cloud_forward_pouch(payload, sizeof(payload), cb_marker, &marker),
                  7);
    zassert_equal(stub.forward_calls, 1);
    zassert_equal(stub.last_payload_len, sizeof(payload));
    zassert_mem_equal(stub.last_payload, payload, sizeof(payload));
    zassert_equal_ptr(stub.last_resp_cb, cb_marker);
    zassert_equal_ptr(stub.last_arg, &marker);

    /* upload_device_cert */
    const uint8_t cert[] = {0xde, 0xad, 0xbe, 0xef};
    stub.cert_rc = -EAGAIN;
    zassert_equal(pouch_gateway_cloud_upload_device_cert(cert, sizeof(cert)), -EAGAIN);
    zassert_equal(stub.cert_calls, 1);
    zassert_equal(stub.last_cert_len, sizeof(cert));
    zassert_mem_equal(stub.last_cert, cert, sizeof(cert));
}

/* Second stub used by the replacement test. */
static int alt_forward_calls;
static int alt_forward_rc;

static int alt_forward(const uint8_t *d, size_t l, pouch_gateway_cloud_block2_cb_t cb, void *a)
{
    ARG_UNUSED(d);
    ARG_UNUSED(l);
    ARG_UNUSED(cb);
    ARG_UNUSED(a);
    alt_forward_calls++;
    return alt_forward_rc;
}

static const struct pouch_gateway_cloud_transport alt_transport = {
    .forward_pouch = alt_forward,
};

ZTEST(cloud, test_register_replaces_previous)
{
    alt_forward_calls = 0;
    alt_forward_rc = 13;

    pouch_gateway_cloud_transport_register(&stub_transport);
    pouch_gateway_cloud_transport_register(&alt_transport);

    zassert_equal(pouch_gateway_cloud_forward_pouch((const uint8_t *) "y", 1, NULL, NULL), 13);
    zassert_equal(alt_forward_calls, 1, "Replacement transport should be active");
    zassert_equal(stub.forward_calls, 0, "Original transport should not be called");
}
