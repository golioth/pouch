/*
 * Copyright (c) 2026 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <string.h>
#include "transport/endpoints/endpoint.h"
#include "transport/bearer.h"
#include "transport/sar/sender.h"
#include "transport/sar/receiver.h"
#include "transport/sar/packet.h"

// End-to-end tests of a sender and a receiver, connected through a link that loses packets.

#define MAXLEN 10
#define PAYLOAD (MAXLEN - POUCH_SAR_TX_PKT_HEADER_LEN)
#define RX_WINDOW 4
#define SHORT_TRANSFER (12 * PAYLOAD + 3)
// More fragments than there are sequence numbers:
#define LONG_TRANSFER (300 * PAYLOAD)
#define MAX_ITERATIONS 10000

struct packet
{
    size_t len;
    uint8_t buf[MAXLEN];
};

K_MSGQ_DEFINE(data_q, sizeof(struct packet), 512, 4);
K_MSGQ_DEFINE(ack_q, sizeof(struct packet), 512, 4);

enum mode
{
    /** The receiver ACKs a batch of fragments at once. */
    MODE_BATCH,
    /** The receiver ACKs every fragment before the next one arrives. */
    MODE_INTERLEAVED,
};

static struct
{
    enum mode mode;
    /** Indexes of fragments the link loses, counting every fragment sent, except FIN. */
    uint64_t drop_fragments;
    /** Indexes of ACKs the link loses. */
    uint64_t drop_acks;
    /** Number of FINs the link loses. */
    int drop_fins;

    int fragments;
    int acks;
    int fins;

    size_t len;
    size_t pulled;
    size_t received;

    bool tx_ended;
    bool tx_success;
    bool rx_ended;
    bool rx_success;
} test;

static uint8_t data_byte(size_t offset)
{
    return (offset * 7) ^ (offset >> 8);
}

static bool drop(uint64_t mask, int index)
{
    return index < 64 && (mask & BIT64(index));
}

static void queue(struct k_msgq *q, const uint8_t *buf, size_t len)
{
    struct packet pkt = {
        .len = len,
    };

    zassert_true(len <= sizeof(pkt.buf));
    memcpy(pkt.buf, buf, len);
    zassert_ok(k_msgq_put(q, &pkt, K_NO_WAIT));
}

static int tx_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    struct pouch_sar_tx_pkt pkt;
    zassert_ok(pouch_sar_tx_pkt_decode(buf, len, &pkt));

    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        if (test.fins++ < test.drop_fins)
        {
            return 0;
        }
    }
    else if (drop(test.drop_fragments, test.fragments++))
    {
        return 0;
    }

    queue(&data_q, buf, len);
    return 0;
}

static int rx_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    if (drop(test.drop_acks, test.acks++))
    {
        return 0;
    }

    queue(&ack_q, buf, len);
    return 0;
}

static void bearer_close(struct pouch_bearer *bearer, bool success) {}

static void bearer_ready(struct pouch_bearer *bearer) {}

static struct pouch_bearer tx_bearer = {
    .send = tx_bearer_send,
    .close = bearer_close,
    .ready = bearer_ready,
    .maxlen = MAXLEN,
};

static struct pouch_bearer rx_bearer = {
    .send = rx_bearer_send,
    .close = bearer_close,
    .ready = bearer_ready,
    .maxlen = MAXLEN,
};

static int endpoint_start(struct pouch_bearer *bearer)
{
    return 0;
}

static enum pouch_result tx_endpoint_send(struct pouch_bearer *bearer, void *dst, size_t *len)
{
    uint8_t *bytes = dst;

    *len = MIN(*len, test.len - test.pulled);
    for (size_t i = 0; i < *len; i++)
    {
        bytes[i] = data_byte(test.pulled++);
    }

    return (test.pulled == test.len) ? POUCH_NO_MORE_DATA : POUCH_MORE_DATA;
}

static void tx_endpoint_end(struct pouch_bearer *bearer, bool success)
{
    zassert_false(test.tx_ended);
    test.tx_ended = true;
    test.tx_success = success;
}

static int rx_endpoint_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    const uint8_t *bytes = buf;

    zassert_true(test.received + len <= test.len);
    for (size_t i = 0; i < len; i++)
    {
        zassert_equal(bytes[i], data_byte(test.received), "offset %u", test.received);
        test.received++;
    }

    return 0;
}

static void rx_endpoint_end(struct pouch_bearer *bearer, bool success)
{
    zassert_false(test.rx_ended);
    test.rx_ended = true;
    test.rx_success = success;
}

static const struct pouch_endpoint tx_endpoint = {
    .start = endpoint_start,
    .send = tx_endpoint_send,
    .end = tx_endpoint_end,
};

static const struct pouch_endpoint rx_endpoint = {
    .start = endpoint_start,
    .recv = rx_endpoint_recv,
    .end = rx_endpoint_end,
};

static struct pouch_sender sender = {
    .endpoint = &tx_endpoint,
};

static struct pouch_receiver receiver = {
    .endpoint = &rx_endpoint,
};

static void pump(void)
{
    struct packet pkt;

    if (test.mode == MODE_BATCH)
    {
        while (k_msgq_get(&ack_q, &pkt, K_NO_WAIT) == 0)
        {
            (void) pouch_sender_recv(&sender, pkt.buf, pkt.len);
        }

        // The receiver's ACK work doesn't run until this thread sleeps, so it ACKs all the
        // fragments at once.
        while (k_msgq_get(&data_q, &pkt, K_NO_WAIT) == 0)
        {
            (void) pouch_receiver_recv(&receiver, pkt.buf, pkt.len);
        }

        return;
    }

    bool idle = false;
    while (!idle)
    {
        idle = true;
        if (k_msgq_get(&ack_q, &pkt, K_NO_WAIT) == 0)
        {
            (void) pouch_sender_recv(&sender, pkt.buf, pkt.len);
            idle = false;
        }

        if (k_msgq_get(&data_q, &pkt, K_NO_WAIT) == 0)
        {
            (void) pouch_receiver_recv(&receiver, pkt.buf, pkt.len);
            // Let the receiver's ACK work run before the next fragment arrives:
            k_yield();
            idle = false;
        }
    }
}

static bool done(void)
{
    // Stop early if the sender gave up, as the receiver will just keep waiting
    return test.tx_ended && (test.rx_ended || !test.tx_success);
}

static void transfer(size_t len)
{
    test.fragments = 0;
    test.acks = 0;
    test.fins = 0;
    test.len = len;
    test.pulled = 0;
    test.received = 0;
    test.tx_ended = false;
    test.rx_ended = false;

    zassert_ok(pouch_receiver_open(&receiver, &rx_bearer, RX_WINDOW));
    zassert_ok(pouch_sender_open(&sender, &tx_bearer));

    for (int i = 0; i < MAX_ITERATIONS && !done(); i++)
    {
        pump();
        k_sleep(K_MSEC(1));
    }

    zassert_true(test.tx_ended && test.tx_success && test.rx_ended && test.rx_success,
                 "mode %d, len %u, lost fragments 0x%llx, lost ACKs 0x%llx, lost FINs %d: "
                 "sender %s, receiver %s",
                 test.mode,
                 len,
                 test.drop_fragments,
                 test.drop_acks,
                 test.drop_fins,
                 !test.tx_ended ? "busy" : (test.tx_success ? "success" : "failed"),
                 !test.rx_ended ? "busy" : (test.rx_success ? "success" : "failed"));
    zassert_equal(test.received, len);

    pouch_sender_close(&sender);
    pouch_receiver_close(&receiver);
    k_sleep(K_MSEC(1));
    k_msgq_purge(&data_q);
    k_msgq_purge(&ack_q);
}

static void before(void *f)
{
    memset(&test, 0, sizeof(test));
}

static void after(void *f)
{
    pouch_sender_close(&sender);
    pouch_receiver_close(&receiver);
    k_sleep(K_MSEC(1));
    k_msgq_purge(&data_q);
    k_msgq_purge(&ack_q);
}

ZTEST_SUITE(transport_sar_exchange, NULL, NULL, before, after, NULL);

static const enum mode modes[] = {MODE_BATCH, MODE_INTERLEAVED};

ZTEST(transport_sar_exchange, test_no_loss)
{
    ARRAY_FOR_EACH(modes, m)
    {
        test.mode = modes[m];
        transfer(SHORT_TRANSFER);
        transfer(LONG_TRANSFER);
        transfer(0);
    }
}

ZTEST(transport_sar_exchange, test_lost_fragment)
{
    ARRAY_FOR_EACH(modes, m)
    {
        test.mode = modes[m];
        transfer(SHORT_TRANSFER);
        int fragments = test.fragments;

        for (int i = 0; i < fragments; i++)
        {
            test.drop_fragments = BIT64(i);
            transfer(SHORT_TRANSFER);
        }
    }
}

ZTEST(transport_sar_exchange, test_lost_fragments)
{
    ARRAY_FOR_EACH(modes, m)
    {
        test.mode = modes[m];
        transfer(SHORT_TRANSFER);
        int fragments = test.fragments;

        for (int i = 0; i < fragments; i++)
        {
            // Two in a row:
            test.drop_fragments = BIT64(i) | BIT64(i + 1);
            transfer(SHORT_TRANSFER);

            // The fragment, and the first one sent after it:
            test.drop_fragments = BIT64(i) | BIT64(i + RX_WINDOW);
            transfer(SHORT_TRANSFER);
        }
    }
}

ZTEST(transport_sar_exchange, test_lost_ack)
{
    ARRAY_FOR_EACH(modes, m)
    {
        test.mode = modes[m];
        test.drop_fragments = 0;
        transfer(SHORT_TRANSFER);
        int acks = test.acks;

        for (int i = 0; i < acks; i++)
        {
            test.drop_acks = BIT64(i);
            transfer(SHORT_TRANSFER);

            test.drop_acks = BIT64(i) | BIT64(i + 1);
            transfer(SHORT_TRANSFER);
        }
    }
}

ZTEST(transport_sar_exchange, test_lost_fragment_and_ack)
{
    ARRAY_FOR_EACH(modes, m)
    {
        test.mode = modes[m];
        transfer(SHORT_TRANSFER);
        int fragments = test.fragments;

        for (int i = 0; i < fragments; i++)
        {
            test.drop_fragments = BIT64(i);
            test.drop_acks = BIT64(i);
            transfer(SHORT_TRANSFER);
        }
    }
}

ZTEST(transport_sar_exchange, test_long_transfer_with_loss)
{
    ARRAY_FOR_EACH(modes, m)
    {
        test.mode = modes[m];
        test.drop_fragments = BIT64(3) | BIT64(17) | BIT64(18) | BIT64(40) | BIT64(63);
        test.drop_acks = BIT64(5) | BIT64(30) | BIT64(31);
        transfer(LONG_TRANSFER);
    }
}
