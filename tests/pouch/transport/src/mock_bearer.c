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

#include <pouch/uplink.h>
#include <pouch/pouch.h>

///////////
// Bearer
///////////

enum bearer_flags
{
    BEARER_CLOSED,
    BEARER_FAILED,
    BEARER_EXPECT_READY,
    BEARER_EXPECT_SEND,
    BEARER_EXPECT_CLOSE,
};

static struct
{
    size_t sent_data;
    atomic_t send_calls;
    atomic_t flags;
} test_bearer;

static void bearer_ready(struct pouch_bearer *bearer)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_READY));
}

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_SEND));
    atomic_inc(&test_bearer.send_calls);
    test_bearer.sent_data += len;
    return 0;
}

static void bearer_close(struct pouch_bearer *bearer, bool success)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE));
    atomic_set_bit(&test_bearer.flags, BEARER_CLOSED);
}

static struct pouch_bearer bearer = {
    .ready = bearer_ready,
    .send = bearer_send,
    .close = bearer_close,
};
