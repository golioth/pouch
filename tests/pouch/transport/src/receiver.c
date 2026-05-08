/*
 * Copyright (c) 2026 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zcbor_decode.h>
#include <stdlib.h>
#include "transport/endpoints/endpoint.h"
#include "transport/bearer.h"
#include "transport/sar/receiver.h"
#include "transport/sar/packet.h"

#include <pouch/uplink.h>
#include <pouch/pouch.h>

#include "mock.h"


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
    atomic_t flags;
    uint8_t ack_seq;
    uint8_t ack_window;
    atomic_t acks;
    struct k_sem sem;
} test_bearer;

static void bearer_ready(struct pouch_bearer *bearer)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_READY));
}

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_SEND));
    struct pouch_sar_rx_pkt pkt;
    zassert_ok(pouch_sar_rx_pkt_decode(buf, len, &pkt));
    atomic_inc(&test_bearer.acks);

    test_bearer.ack_seq = pkt.seq;
    test_bearer.ack_window = pkt.window;

    k_sem_give(&test_bearer.sem);

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

static void reset_mocks(void)
{
    memset(&test_endpoint, 0, sizeof(test_endpoint));
    memset(&test_bearer, 0, sizeof(test_bearer));
    bearer.maxlen = 10;

    k_sem_init(&test_bearer.sem, 0, 1);
}


static struct pouch_receiver receiver = {
    .endpoint = &endpoint,
};

static void reset(void *unused)
{
    reset_mocks();
    struct k_work_sync sync;
    k_work_cancel_delayable_sync(&receiver.work, &sync);

    receiver = (struct pouch_receiver){
        .endpoint = &endpoint,
    };
}

ZTEST_SUITE(transport_sar_receiver, NULL, NULL, reset, NULL, NULL);

ZTEST(transport_sar_receiver, test_open_and_close)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 1));

    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);

    pouch_receiver_close(&receiver);

    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_invalid_bearer_maxlen)
{
    bearer.maxlen = 1;
    zassert_equal(pouch_receiver_open(&receiver, &bearer, 1), -EINVAL);

    zassert_false(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));
}

ZTEST(transport_sar_receiver, test_invalid_window)
{
    zassert_equal(pouch_receiver_open(&receiver, &bearer, 128), -EINVAL);

    zassert_false(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));
}

ZTEST(transport_sar_receiver, test_send_first_ack)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));
    zassert_equal(test_bearer.ack_seq, 0xff);
    zassert_equal(test_bearer.ack_window, 4);
}

ZTEST(transport_sar_receiver, test_repeat_first_ack)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));
    zassert_equal(test_bearer.ack_seq, 0xff);
    zassert_equal(test_bearer.ack_window, 4);
    zassert_equal(test_bearer.acks, 1);

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(10000)));
    zassert_equal(test_bearer.ack_seq, 0xff);
    zassert_equal(test_bearer.ack_window, 4);
    zassert_equal(test_bearer.acks, 2);
}
