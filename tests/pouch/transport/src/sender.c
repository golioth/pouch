/*
 * Copyright (c) 2026 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zcbor_decode.h>
#include <stdlib.h>
#include "transport/endpoints/endpoint.h"
#include "transport/bearer.h"
#include "transport/sar/sender.h"
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
    BEARER_SENT_FIN,
    BEARER_SENT_LAST_PACKET,
    BEARER_EXPECT_READY,
    BEARER_EXPECT_SEND,
    BEARER_EXPECT_CLOSE,
    BEARER_FAIL_SEND_ONCE,
};

static struct
{
    size_t sent_data;
    atomic_t sent_packets;
    atomic_t flags;
} test_bearer;

static void bearer_ready(struct pouch_bearer *bearer)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_READY));
}

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_EXPECT_SEND));

    // Support conditional failure for testing error paths
    if (atomic_test_and_clear_bit(&test_bearer.flags, BEARER_FAIL_SEND_ONCE))
    {
        return -EIO;
    }

    struct pouch_sar_tx_pkt pkt;
    zassert_ok(pouch_sar_tx_pkt_decode(buf, len, &pkt));
    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        atomic_set_bit(&test_bearer.flags, BEARER_SENT_FIN);
        atomic_inc(&test_bearer.sent_packets);
    }
    else
    {
        // FIN packets don't have a seq, but for everything else, we want to validate the seqnum:
        zassert_equal(atomic_inc(&test_bearer.sent_packets), pkt.seq);
    }
    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_LAST)
    {
        atomic_set_bit(&test_bearer.flags, BEARER_SENT_LAST_PACKET);
    }

    test_bearer.sent_data += pkt.len;

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
}


static struct pouch_sender sender = {
    .endpoint = &endpoint,
};

static void reset(void *unused)
{
    reset_mocks();

    sender = (struct pouch_sender){
        .endpoint = &endpoint,
    };
}

ZTEST_SUITE(transport_sar_sender, NULL, NULL, reset, NULL, NULL);

ZTEST(transport_sar_sender, test_open_and_close)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);

    zassert_ok(pouch_sender_open(&sender, &bearer));

    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));

    // since we haven't received any acks yet, calling ready should just be a noop:
    pouch_sender_ready(&sender);

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);

    pouch_sender_close(&sender);

    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
}

ZTEST(transport_sar_sender, test_invalid_bearer_maxlen)
{
    bearer.maxlen = 1;
    zassert_equal(pouch_sender_open(&sender, &bearer), -EINVAL);

    zassert_false(atomic_test_bit(&test_endpoint.flags, ENDPOINT_STARTED));
}

ZTEST(transport_sar_sender, test_recv_ack_no_window)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    const struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 0,  // don't accept packets yet
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // receiving an ack with a zero window: not expecting a TX
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
}

ZTEST(transport_sar_sender, test_recv_ack_no_data)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    const struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 1,  // get one packet
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    // not expecting a call to the bearer - we have no data
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 1);  // requested one packet
}

ZTEST(transport_sar_sender, test_recv_ack_send_data)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    const struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 1,  // get one packet
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 4;

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 1);  // requested one packet
    zassert_equal(atomic_get(&test_bearer.sent_packets), 1);  // sent one packet
    zassert_equal(test_bearer.sent_data, 4, "got %u", test_bearer.sent_data);
}

ZTEST(transport_sar_sender, test_recv_ack_window)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    const struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,  // should send 4 packets
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 10 * (bearer.maxlen - 2);  // give it more data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);  // requested four packets
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // sent four packets
    zassert_equal(test_bearer.sent_data, 4 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);
}

ZTEST(transport_sar_sender, test_recv_acks)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,  // should send 4 packets
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 10 * (bearer.maxlen - 2);  // give it more data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);  // requested four packets
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // sent four packets
    zassert_equal(test_bearer.sent_data, 4 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);

    // ack the first seq, should send one more packet, as the window moved
    ack.seq = 0;
    pouch_sar_rx_pkt_encode(&ack, buf);
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 5);  // requested one more packet
    zassert_equal(atomic_get(&test_bearer.sent_packets), 5);  // one more packet
    zassert_equal(test_bearer.sent_data, 5 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);
}

ZTEST(transport_sar_sender, test_recv_ack_out_of_order)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,  // should send 4 packets
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 10 * (bearer.maxlen - 2);  // give it more data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);  // requested four packets
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // sent four packets
    zassert_equal(test_bearer.sent_data, 4 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);

    // ack the last seq. Should send four more packets, as the window moved.
    // Silently accepting skipped seqs.
    ack.seq = 3;
    pouch_sar_rx_pkt_encode(&ack, buf);
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 8);  // requested one more packet
    zassert_equal(atomic_get(&test_bearer.sent_packets), 8);  // one more packet
    zassert_equal(test_bearer.sent_data, 8 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);

    // ack an out of order seq, effectively shrinking the window - should fail
    ack.seq = 2;
    pouch_sar_rx_pkt_encode(&ack, buf);
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 8);  // requested nothing
    zassert_equal(atomic_get(&test_bearer.sent_packets), 8);  // sent nothing
}

ZTEST(transport_sar_sender, test_recv_ack_max_window)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = POUCH_SAR_WINDOW_MAX,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 100000;  // give it more data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), POUCH_SAR_WINDOW_MAX);
    zassert_equal(atomic_get(&test_bearer.sent_packets), POUCH_SAR_WINDOW_MAX);
    zassert_equal(test_bearer.sent_data,
                  POUCH_SAR_WINDOW_MAX * (bearer.maxlen - 2),
                  "got %u",
                  test_bearer.sent_data);
}

ZTEST(transport_sar_sender, test_recv_ack_too_large_window)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = POUCH_SAR_WINDOW_MAX + 1,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 100;
    // should reject the window, it's too large:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
}

ZTEST(transport_sar_sender, test_recv_ack_unknown_seq)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0,  // not sent yet. Should be 0xff
        .window = 4,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // we have some data:
    test_endpoint.available_data = 100;
    // should reject the ack, as we haven't sent that sequence number yet:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
}

ZTEST(transport_sar_sender, test_recv_ack_shrinking_window)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    test_endpoint.available_data = 10 * (bearer.maxlen - 2);  // give it more data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);  // requested four packets
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // sent four packets
    zassert_equal(test_bearer.sent_data, 4 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);

    ack.seq = 0;
    ack.window = 3;  // shrinks the window so that the sender can't send more packets
    pouch_sar_rx_pkt_encode(&ack, buf);
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);  // no change
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // no change
}

// Window should not shrink more than the already granted window
ZTEST(transport_sar_sender, test_recv_ack_invalid_shrinking_window)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    test_endpoint.available_data = 10 * (bearer.maxlen - 2);  // give it more data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);  // requested four packets
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // sent four packets
    zassert_equal(test_bearer.sent_data, 4 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);

    ack.seq = 1;
    ack.window = 1;  // shrinks the window to exclude the last sent packet
    pouch_sar_rx_pkt_encode(&ack, buf);

    // should reject the ack, as we have exceeded the window:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_not_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
}

// Endpoint returns no more data after originally claiming there's more
ZTEST(transport_sar_sender, test_no_more_data)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    test_endpoint.available_data = 3 * (bearer.maxlen - 2);  // give it LESS data than it needs
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);
    zassert_equal(atomic_get(&test_bearer.sent_packets), 3);
    zassert_equal(test_bearer.sent_data, 3 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);
    zassert_false(atomic_test_bit(&test_bearer.flags, BEARER_SENT_LAST_PACKET));

    // there's no more data after all:
    test_endpoint.available_data = 0;
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_CLOSED);

    // should request more data, but come back empty.
    pouch_sender_ready(&sender);
    zassert_equal(atomic_get(&test_endpoint.send_calls), 5);  // requested another packet
    // The packet should get sent, but the amount of data should remain the same
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);  // sent an empty packet
    zassert_equal(test_bearer.sent_data, 3 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_SENT_LAST_PACKET));
}

// Send FIN packet once last seq has been acknowledged:
ZTEST(transport_sar_sender, test_send_fin)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    test_endpoint.available_data = 3 * (bearer.maxlen - 2);
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 4);
    zassert_equal(atomic_get(&test_bearer.sent_packets), 3);
    zassert_equal(test_bearer.sent_data, 3 * (bearer.maxlen - 2), "got %u", test_bearer.sent_data);

    // there's no more data, close the endpoint:
    test_endpoint.available_data = 5;
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_CLOSED);

    pouch_sender_ready(&sender);
    zassert_equal(atomic_get(&test_endpoint.send_calls), 5);
    zassert_equal(atomic_get(&test_bearer.sent_packets), 4);
    zassert_equal(test_bearer.sent_data,
                  3 * (bearer.maxlen - 2) + 5,
                  "got %u",
                  test_bearer.sent_data);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_SENT_LAST_PACKET));
    zassert_false(atomic_test_bit(&test_bearer.flags, BEARER_SENT_FIN));

    // ack the second to last packet, should not close the link:
    ack.seq = 2;
    pouch_sar_rx_pkt_encode(&ack, buf);
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_false(atomic_test_bit(&test_bearer.flags, BEARER_SENT_FIN));

    // ack the last packet:
    ack.seq = 3;
    pouch_sar_rx_pkt_encode(&ack, buf);

    // should send FIN and close the link:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 5);  // didn't request any additional data
    // sent FIN:
    zassert_equal(atomic_get(&test_bearer.sent_packets), 5);  // didn't send anything
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_SENT_FIN));
}

// No data on link
ZTEST(transport_sar_sender, test_empty_transfer)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0xff,
        .window = 4,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // no data on first packet:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_CLOSED);
    test_endpoint.available_data = 0;
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);

    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 1);
    zassert_equal(atomic_get(&test_bearer.sent_packets), 1);
    zassert_equal(test_bearer.sent_data, 0);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_SENT_LAST_PACKET));

    // ack the packet:
    ack.seq = 0;
    pouch_sar_rx_pkt_encode(&ack, buf);

    // should send FIN and close the link:
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));
    zassert_equal(atomic_get(&test_endpoint.send_calls), 1);  // didn't request any additional data
    // sent FIN:
    zassert_equal(atomic_get(&test_bearer.sent_packets), 2);
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_SENT_FIN));
}

ZTEST(transport_sar_sender, test_bearer_send_packet_fails)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    // Prepare for sending data
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_DATA_REQ);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_SEND);
    test_endpoint.available_data = 5;

    // Make bearer_send fail on first attempt
    atomic_set_bit(&test_bearer.flags, BEARER_FAIL_SEND_ONCE);

    // Send ACK with window to trigger sending
    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = POUCH_SAR_SEQ_MAX,
        .window = 3,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // bearer_send will fail with -EIO, sender should log error and stop
    zassert_ok(pouch_sender_recv(&sender, buf, sizeof(buf)));

    // Verify endpoint was called to get data
    zassert_equal(atomic_get(&test_endpoint.send_calls), 1);

    // Verify no packet was sent due to bearer failure
    zassert_equal(atomic_get(&test_bearer.sent_packets), 0);

    // Sender state should remain consistent (ready to retry or close)
    zassert_equal(sender.seq, 0);
    zassert_not_equal(sender.window, 0);
}

ZTEST(transport_sar_sender, test_double_close)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);

    // First close - should succeed
    pouch_sender_close(&sender);

    // Verify state after first close
    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_true(atomic_test_bit(&test_endpoint.flags, ENDPOINT_ENDED));
    zassert_equal(sender.bearer, NULL);
    zassert_equal(sender.buf, NULL);

    // Second close - should not crash or double-free
    // Note: This is currently unsafe in the implementation!
    // The test documents the bug by checking current behavior
    pouch_sender_close(&sender);

    // After second close, state should remain consistent
    zassert_equal(sender.bearer, NULL);
    zassert_equal(sender.buf, NULL);
}

ZTEST(transport_sar_sender, test_recv_after_close)
{
    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_START);
    zassert_ok(pouch_sender_open(&sender, &bearer));

    atomic_set_bit(&test_endpoint.flags, ENDPOINT_EXPECT_END);
    atomic_set_bit(&test_bearer.flags, BEARER_EXPECT_CLOSE);

    // Close the sender
    pouch_sender_close(&sender);

    zassert_true(atomic_test_bit(&test_bearer.flags, BEARER_CLOSED));
    zassert_equal(sender.bearer, NULL);

    // Try to receive ACK after close - should return -EBUSY immediately
    struct pouch_sar_rx_pkt ack = {
        .code = POUCH_RECEIVER_CODE_ACK,
        .seq = 0,
        .window = 3,
    };
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    pouch_sar_rx_pkt_encode(&ack, buf);

    // Should return -EBUSY without accessing freed memory
    zassert_equal(pouch_sender_recv(&sender, buf, sizeof(buf)), -EBUSY);

    // Verify no crash or use-after-free occurred
    zassert_equal(sender.bearer, NULL);
    zassert_equal(sender.buf, NULL);
}
