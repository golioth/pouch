/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * rpmsg/UART framing tests.
 *
 * The rpmsg and UART device adapters carry Pouch Serial frames over a link
 * using the length-delimited framing in uart_framing.[ch]. This suite tests
 * that framing two ways:
 *
 *   1. Directly: encode/decode round-trips, resynchronization, and rejection
 *      of malformed frames.
 *   2. End to end: a full broker <-> device serial exchange (the same one
 *      exercised by tests/pouch/serial/exchange, using stub endpoints so no
 *      gateway or cloud is required) where every frame on the wire is passed
 *      through encode + a byte-at-a-time decode. If the framing corrupted or
 *      dropped any frame, the exchange would not converge and the received
 *      payloads would not match.
 */

#include "stub_endpoints.h"
#include "uart_framing.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <pouch/transport/serial/broker.h>
#include <pouch/transport/serial/device.h>

#define MAX_FRAME_SIZE 64
#define MAX_PUMP_ITER 200

static struct pouch_serial_broker *test_broker;
static bool broker_done;
static bool broker_success;

static void broker_ready(const struct pouch_serial_broker *broker)
{
    (void) broker;
}

static void broker_end(const struct pouch_serial_broker *broker, bool success)
{
    (void) broker;
    broker_done = true;
    broker_success = success;
}

static const struct pouch_serial_broker_adapter adapter = {
    .ready = broker_ready,
    .end = broker_end,
};

static void device_ready(void)
{
    pouch_serial_broker_notify(test_broker);
}

/*
 * Pass a serial frame through the UART framing: encode it, then feed the
 * encoded bytes one at a time into a fresh decoder. Asserts the decoder
 * reproduces exactly the original frame, and returns it in @p out.
 */
static size_t roundtrip_frame(const uint8_t *frame, size_t len, uint8_t *out, size_t out_size)
{
    uint8_t wire[MAX_FRAME_SIZE + POUCH_UART_FRAME_OVERHEAD];

    size_t wire_len = pouch_uart_frame_encode(wire, sizeof(wire), frame, len);
    zassert_equal(wire_len, len + POUCH_UART_FRAME_OVERHEAD, "unexpected encoded length");

    struct pouch_uart_framer framer;
    pouch_uart_framer_init(&framer, out, out_size);

    size_t decoded = 0;
    for (size_t i = 0; i < wire_len; i++)
    {
        decoded = pouch_uart_framer_feed(&framer, wire[i]);
    }

    zassert_equal(decoded, len, "decoded length %zu != %zu", decoded, len);
    zassert_mem_equal(out, frame, len, "framing corrupted the frame");
    return decoded;
}

/* Pump frames between broker and device, routing each through the framing. */
static int pump_framed(int max_iter)
{
    uint8_t buf[MAX_FRAME_SIZE];
    uint8_t decoded[MAX_FRAME_SIZE];
    int iter;

    for (iter = 0; iter < max_iter; iter++)
    {
        size_t blen = pouch_serial_broker_frame_get(test_broker, buf, sizeof(buf));
        if (blen > 0)
        {
            size_t dlen = roundtrip_frame(buf, blen, decoded, sizeof(decoded));
            zassert_ok(pouch_serial_device_recv(decoded, dlen), "device_recv failed");
            continue;
        }

        size_t dlen0 = pouch_serial_device_frame_get(buf, sizeof(buf));
        if (dlen0 > 0)
        {
            size_t dlen = roundtrip_frame(buf, dlen0, decoded, sizeof(decoded));
            zassert_ok(pouch_serial_broker_recv(test_broker, decoded, dlen), "broker_recv failed");
            continue;
        }

        break;
    }

    zassert_true(iter < max_iter, "exchange did not converge within %d iterations", max_iter);
    return iter;
}

static void reset_parties(void)
{
    free(test_broker);
    test_broker = pouch_serial_broker_create(&adapter);
    zassert_not_null(test_broker, "broker_create returned NULL");

    stubs_reset();
    pouch_serial_device_init(device_ready);

    broker_done = false;
    broker_success = false;
}

/* --- direct framing tests --- */

ZTEST(rpmsg_framing, test_roundtrip)
{
    const uint8_t frame[] = {0x81, 0x02, 0x03, 0x04, 0x05};
    uint8_t out[16];

    size_t n = roundtrip_frame(frame, sizeof(frame), out, sizeof(out));
    zassert_equal(n, sizeof(frame), "length mismatch");
}

ZTEST(rpmsg_framing, test_resync_after_garbage)
{
    uint8_t out[16];
    struct pouch_uart_framer framer;
    pouch_uart_framer_init(&framer, out, sizeof(out));

    /* Garbage bytes before a frame must be ignored. */
    zassert_equal(pouch_uart_framer_feed(&framer, 0x11), 0, "garbage produced a frame");
    zassert_equal(pouch_uart_framer_feed(&framer, 0x22), 0, "garbage produced a frame");

    const uint8_t wire[] = {POUCH_UART_FRAME_SOF, 0x00, 0x02, 0xAA, 0xBB};
    size_t got = 0;
    for (size_t i = 0; i < sizeof(wire); i++)
    {
        got = pouch_uart_framer_feed(&framer, wire[i]);
    }

    zassert_equal(got, 2, "frame not recovered after garbage");
    zassert_equal(out[0], 0xAA, "payload mismatch");
    zassert_equal(out[1], 0xBB, "payload mismatch");
}

ZTEST(rpmsg_framing, test_rejects_oversize)
{
    uint8_t out[4];
    struct pouch_uart_framer framer;
    pouch_uart_framer_init(&framer, out, sizeof(out));

    /* Declared length 5 exceeds the 4-byte buffer: the frame must be dropped
     * and no spurious frame reported. */
    const uint8_t wire[] = {POUCH_UART_FRAME_SOF, 0x00, 0x05, 1, 2, 3, 4, 5};
    for (size_t i = 0; i < sizeof(wire); i++)
    {
        zassert_equal(pouch_uart_framer_feed(&framer, wire[i]), 0, "oversize frame not rejected");
    }
}

ZTEST(rpmsg_framing, test_encode_rejects_bad_args)
{
    uint8_t out[8];
    const uint8_t frame[] = {1, 2, 3};

    zassert_equal(pouch_uart_frame_encode(out, sizeof(out), frame, 0), 0, "zero length accepted");
    zassert_equal(pouch_uart_frame_encode(out, 4, frame, sizeof(frame)), 0, "overflow not caught");
}

ZTEST_SUITE(rpmsg_framing, NULL, NULL, NULL, NULL, NULL);

/* --- end-to-end exchange through the framing --- */

static const uint8_t info_payload[] = "device-info-stub";
static const uint8_t server_cert_payload[] = "server-certificate-data";
static const uint8_t device_cert_payload[] = "device-certificate-data";
static const uint8_t downlink_payload[] = "downlink-payload";
static const uint8_t uplink_payload[] = "uplink-payload";

static void load_default_payloads(void)
{
    stub_sender_set_data(&device_stubs.info, info_payload, sizeof(info_payload));
    stub_sender_set_data(&broker_stubs.server_cert, server_cert_payload, sizeof(server_cert_payload));
    stub_sender_set_data(&device_stubs.device_cert, device_cert_payload, sizeof(device_cert_payload));
    stub_sender_set_data(&broker_stubs.downlink, downlink_payload, sizeof(downlink_payload));
    stub_sender_set_data(&device_stubs.uplink, uplink_payload, sizeof(uplink_payload));
}

ZTEST(rpmsg_exchange, test_exchange_survives_framing)
{
    reset_parties();
    load_default_payloads();

    zassert_ok(pouch_serial_broker_start(test_broker), "broker_start failed");
    pump_framed(MAX_PUMP_ITER);

    zassert_true(broker_done, "exchange did not complete");
    zassert_true(broker_success, "exchange ended with failure");

    /* Device -> broker payloads survived the framing round-trip. */
    zassert_equal(broker_stubs.uplink.rx_len, sizeof(uplink_payload), "uplink length");
    zassert_mem_equal(broker_stubs.uplink.rx_buf, uplink_payload, sizeof(uplink_payload),
                      "uplink data");
    zassert_equal(broker_stubs.info.rx_len, sizeof(info_payload), "info length");
    zassert_mem_equal(broker_stubs.info.rx_buf, info_payload, sizeof(info_payload), "info data");

    /* Broker -> device payloads survived too. */
    zassert_equal(device_stubs.downlink.rx_len, sizeof(downlink_payload), "downlink length");
    zassert_mem_equal(device_stubs.downlink.rx_buf, downlink_payload, sizeof(downlink_payload),
                      "downlink data");
}

static void exchange_teardown(void *f)
{
    (void) f;
    free(test_broker);
    test_broker = NULL;
}

ZTEST_SUITE(rpmsg_exchange, NULL, NULL, NULL, exchange_teardown, NULL);
