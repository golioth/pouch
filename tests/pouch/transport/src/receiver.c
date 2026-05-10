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

ZTEST(transport_sar_receiver, test_invalid_packet_too_short)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t buf[1] = {0};
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, sizeof(buf)));

    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_invalid_packet_not_first)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t buf[2] = {0};
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, sizeof(buf)));

    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_invalid_duplicate_first_packet)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t buf[2] = {POUCH_SAR_TX_PKT_FLAG_FIRST};
    zassert_ok(pouch_receiver_recv(&receiver, buf, sizeof(buf)));

    // FIRST again
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, sizeof(buf)));

    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_rx_single_packet_transfer)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        // this is both the first and the last packet
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 1);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_rx_multi_packet_transfer)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    pkt.flags = 0;
    pkt.seq++;
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 2);

    pkt.flags = POUCH_SAR_TX_PKT_FLAG_LAST;
    pkt.seq++;
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 3);

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 3);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_rx_fail_duplicate_last)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    // send last:
    pkt.flags = POUCH_SAR_TX_PKT_FLAG_LAST;
    pkt.seq++;
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 2);

    // do it again:
    pkt.seq++;
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 2);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_invalid_fin_too_long)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        // this is both the first and the last packet
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));
    // corrupt the length:
    len = sizeof(buf);

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 1);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}


ZTEST(transport_sar_receiver, test_invalid_fin_too_short)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        // this is both the first and the last packet
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));
    // corrupt the length:
    len = 1;

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 1);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_fin_abrupt)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    // not expecting FIN before last packet:
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 1);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_fin_unexpected)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    uint8_t buf[3];
    size_t len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    // not expecting FIN before any packets:
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 0);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_fin_repeated)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    uint8_t data = 0xaa;
    uint8_t buf[3];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        // this is both the first and the last packet
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.received_data, 1);

    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    // FIN again:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_equal(test_endpoint.recv_calls, 1);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_receiver, test_fin_without_first_packet)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    // Send FIN directly without first packet
    struct pouch_sar_tx_pkt fin = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN | POUCH_SAR_TX_PKT_FLAG_IDLE,
    };
    uint8_t buf[3];
    size_t len = sizeof(buf);
    zassert_ok(pouch_sar_tx_pkt_encode(&fin, buf, &len));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_receiver_recv(&receiver, buf, len));

    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}
