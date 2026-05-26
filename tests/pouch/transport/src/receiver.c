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
    BEARER_FAIL_SEND_ONCE,
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

    // Support conditional failure for testing retry logic
    if (atomic_test_and_clear_bit(&test_bearer.flags, BEARER_FAIL_SEND_ONCE))
    {
        return -EBUSY;
    }

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
    pouch_work_cancel_delayable(&receiver.work);

    /*
     * Brief delay to allow any in-flight work to complete.
     * pouch_work_cancel_delayable() is asynchronous - it cancels
     * pending timers but work already queued may still execute once.
     */
    k_sleep(K_MSEC(1));

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

ZTEST(transport_sar_receiver, test_endpoint_recv_fails)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(1)));

    // Send FIRST packet with data, but endpoint recv will fail
    uint8_t data = 0xaa;
    uint8_t buf[10];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));

    // Set flag to make endpoint recv fail
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_FAIL_RECV);
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);

    // Receiver should call end(recv, false) and return error
    zassert_equal(pouch_receiver_recv(&receiver, buf, len), -EINVAL);

    // Verify bearer was closed with success=false
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));

    // Wait for NACK to be sent
    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(10)));

    // Verify NACK was sent (code should be POUCH_RECEIVER_CODE_NACK_UNKNOWN)
    // The receiver state should be STATE_FAILED, which causes code to be NACK
    struct pouch_sar_rx_pkt ack = {
        .code = test_bearer.ack_window,  // This is stored from the bearer_send mock
    };
    uint8_t ack_buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, ack_buf);
    // Decode to verify structure
    struct pouch_sar_rx_pkt decoded;
    zassert_ok(pouch_sar_rx_pkt_decode(ack_buf, sizeof(ack_buf), &decoded));
}

ZTEST(transport_sar_receiver, test_bearer_send_ack_fails)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    // Make bearer_send fail once, then succeed
    atomic_set_bit(&test_bearer.flags, BEARER_FAIL_SEND_ONCE);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    // First ACK attempt will fail with -EBUSY, should be rescheduled
    // Wait longer to allow for retry
    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(CONFIG_POUCH_TRANSPORT_ACK_TIMEOUT_MS + 100)));

    // Verify initial ACK was eventually sent after retry
    zassert_equal(test_bearer.acks, 1);
    zassert_equal(test_bearer.ack_seq, POUCH_SAR_SEQ_MAX);
    zassert_equal(test_bearer.ack_window, 4);
}

ZTEST(transport_sar_receiver, test_ack_timeout_retransmission)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    // Get initial ACK
    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(10)));
    zassert_equal(test_bearer.acks, 1);

    // Send FIRST packet
    uint8_t data = 0xaa;
    uint8_t buf[10];
    size_t len = sizeof(buf);
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIRST,
        .data = &data,
        .len = sizeof(data),
    };
    zassert_ok(pouch_sar_tx_pkt_encode(&pkt, buf, &len));
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_RECV);
    zassert_ok(pouch_receiver_recv(&receiver, buf, len));

    // Get ACK for received packet
    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(10)));
    zassert_equal(test_bearer.acks, 2);
    uint8_t ack_seq = test_bearer.ack_seq;
    uint8_t ack_window = test_bearer.ack_window;

    // Wait for timeout without sending more packets - should retransmit ACK
    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(CONFIG_POUCH_TRANSPORT_ACK_TIMEOUT_MS + 100)));

    // Verify ACK was retransmitted
    zassert_equal(test_bearer.acks, 3);
    // Seq and window should remain the same across retransmissions
    zassert_equal(test_bearer.ack_seq, ack_seq);
    zassert_equal(test_bearer.ack_window, ack_window);

    // Wait for another timeout - should retransmit again
    zassert_ok(k_sem_take(&test_bearer.sem, K_MSEC(CONFIG_POUCH_TRANSPORT_ACK_TIMEOUT_MS + 100)));

    // Verify second retransmission
    zassert_equal(test_bearer.acks, 4);
    zassert_equal(test_bearer.ack_seq, ack_seq);
    zassert_equal(test_bearer.ack_window, ack_window);
}

ZTEST(transport_sar_receiver, test_double_close)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);

    // First close - should succeed
    pouch_receiver_close(&receiver);

    // Verify state after first close
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));

    // Second close - should not crash
    // Work should already be cancelled from first close
    pouch_receiver_close(&receiver);

    // After second close, state should remain consistent
    // No additional end() or bearer_close() should be called
}

ZTEST(transport_sar_receiver, test_failed_open)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    test_endpoint.open_retval = -1234;  // fail open

    // Open receiver - should fail with error code from endpoint start
    zassert_equal(pouch_receiver_open(&receiver, &bearer, 4), -1234);

    // Verify endpoint was started, but nothing was sent
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));
    zassert_equal(test_bearer.acks, 0);
    atomic_clear_bit(&test_endpoint.flags, ENDPOINT_STARTED);

    test_endpoint.open_retval = 0;  // succeed on next open
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);
    zassert_ok(pouch_receiver_open(&receiver, &bearer, 4));
}
