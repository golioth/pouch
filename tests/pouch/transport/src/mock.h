/*
 * Copyright (c) 2026 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zcbor_decode.h>
#include <stdlib.h>
#include <stdio.h>
#include "transport/endpoints/endpoint.h"
#include "transport/bearer.h"
#include "transport/sar/sender.h"
#include "transport/sar/packet.h"

#include <pouch/uplink.h>
#include <pouch/pouch.h>

// Common mocks for bearer tests

/////////////
// Endpoint
/////////////

enum endpoint_flags
{
    ENDPOINT_CLOSED,
    ENDPOINT_STARTED,
    ENDPOINT_ENDED,
    ENDPOINT_FAILED,
    ENDPOINT_EXPECT_START,
    ENDPOINT_EXPECT_DATA_REQ,
    ENDPOINT_EXPECT_END,
};

static struct
{
    size_t available_data;
    atomic_t send_calls;
    atomic_t flags;
} test_endpoint;

static int start(struct pouch_bearer *bearer)
{
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START));
    zassert_false(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_STARTED);
    return 0;
}

static enum pouch_result send(struct pouch_bearer *bearer, void *buf, size_t *len)
{
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));
    zassert_false(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));

    atomic_inc(&test_endpoint.send_calls);

    if (atomic_test_bit(&test_endpoint.flags, ENDPOINT_FAILED))
    {
        return POUCH_ERROR;
    }

    if (*len > test_endpoint.available_data)
    {
        *len = test_endpoint.available_data;
    }

    test_endpoint.available_data -= *len;

    uint8_t *b = buf;
    for (int i = 0; i < *len; i++)
    {
        b[i] = i;
    }

    return atomic_test_bit(&test_endpoint.flags, ENDPOINT_CLOSED) ? POUCH_NO_MORE_DATA
                                                                  : POUCH_MORE_DATA;
}

static void end(struct pouch_bearer *bearer, bool success)
{
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));
    zassert_false(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_ENDED);
}

static const struct pouch_endpoint endpoint = {
    .start = start,
    .send = send,
    .end = end,
};
