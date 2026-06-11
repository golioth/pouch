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
    pouch_gateway_cloud_upload_chunk_cb_t last_chunk_cb;
    void *last_chunk_arg;
    pouch_gateway_cloud_block2_cb_t last_resp_cb;
    void *last_resp_arg;

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

static int stub_forward(pouch_gateway_cloud_upload_chunk_cb_t chunk_cb,
                        void *chunk_arg,
                        pouch_gateway_cloud_block2_cb_t resp_cb,
                        void *resp_arg)
{
    stub.forward_calls++;
    stub.last_chunk_cb = chunk_cb;
    stub.last_chunk_arg = chunk_arg;
    stub.last_resp_cb = resp_cb;
    stub.last_resp_arg = resp_arg;

    /* Drain the streaming source into a linear buffer so tests can
     * inspect the aggregated payload.
     */
    stub.last_payload_len = 0;

    if (chunk_cb != NULL)
    {
        while (stub.last_payload_len < sizeof(stub.last_payload))
        {
            size_t chunk_len = 0;
            bool is_last = false;
            int err = chunk_cb(stub.last_payload + stub.last_payload_len,
                               sizeof(stub.last_payload) - stub.last_payload_len,
                               &chunk_len,
                               &is_last,
                               chunk_arg);
            if (err)
            {
                return err;
            }
            stub.last_payload_len += chunk_len;
            if (is_last)
            {
                break;
            }
        }
    }

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

static int noop_chunk_cb(uint8_t *buf, size_t buf_size, size_t *chunk_len, bool *is_last, void *arg)
{
    ARG_UNUSED(buf);
    ARG_UNUSED(buf_size);
    ARG_UNUSED(arg);
    *chunk_len = 0;
    *is_last = true;
    return 0;
}

ZTEST(cloud, test_dispatch_with_no_transport_returns_enodev)
{
    zassert_equal(pouch_gateway_cloud_ensure_ready(),
                  0,
                  "ensure_ready is optional and must succeed when not registered");

    zassert_equal(pouch_gateway_cloud_forward_pouch(noop_chunk_cb, NULL, NULL, NULL), -ENODEV);

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

struct fixed_source
{
    const uint8_t *data;
    size_t len;
    size_t offset;
};

static int fixed_chunk_cb(uint8_t *buf,
                          size_t buf_size,
                          size_t *chunk_len,
                          bool *is_last,
                          void *arg)
{
    struct fixed_source *src = arg;
    size_t take = MIN(buf_size, src->len - src->offset);

    memcpy(buf, src->data + src->offset, take);
    src->offset += take;
    *chunk_len = take;
    *is_last = (src->offset == src->len);
    return 0;
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
    struct fixed_source src = {.data = payload, .len = sizeof(payload)};
    int marker = 42;
    pouch_gateway_cloud_block2_cb_t cb_marker =
        (pouch_gateway_cloud_block2_cb_t) (uintptr_t) 0xfeed;

    stub.forward_rc = 7;
    zassert_equal(pouch_gateway_cloud_forward_pouch(fixed_chunk_cb, &src, cb_marker, &marker), 7);
    zassert_equal(stub.forward_calls, 1);
    zassert_equal(stub.last_payload_len, sizeof(payload));
    zassert_mem_equal(stub.last_payload, payload, sizeof(payload));
    zassert_equal_ptr(stub.last_resp_cb, cb_marker);
    zassert_equal_ptr(stub.last_resp_arg, &marker);

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

static int alt_forward(pouch_gateway_cloud_upload_chunk_cb_t chunk_cb,
                       void *chunk_arg,
                       pouch_gateway_cloud_block2_cb_t resp_cb,
                       void *resp_arg)
{
    ARG_UNUSED(chunk_cb);
    ARG_UNUSED(chunk_arg);
    ARG_UNUSED(resp_cb);
    ARG_UNUSED(resp_arg);
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

    zassert_equal(pouch_gateway_cloud_forward_pouch(noop_chunk_cb, NULL, NULL, NULL), 13);
    zassert_equal(alt_forward_calls, 1, "Replacement transport should be active");
    zassert_equal(stub.forward_calls, 0, "Original transport should not be called");
}
