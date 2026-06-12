/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Serial exchange integration test.
 *
 * Exercises a full broker ↔ device serial exchange using a mock transport
 * adapter that synchronously pumps frames between the two parties.  Both sets
 * of endpoints are replaced by stubs (see stub_endpoints.c), so the test
 * validates the serial transport layer in isolation from the real gateway and
 * device transport stacks.
 */

#include "stub_endpoints.h"

#include <errno.h>
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
 * Pump frames between broker and device until both sides are idle.
 *
 * Each iteration delivers a single frame: first checking the broker for a
 * pending frame, then the device.  Single-frame delivery mirrors the
 * sequential nature of a serial link and prevents stale intermediate ACKs
 * from arriving after a sender has closed its channel.
 *
 * Returns the number of iterations performed.
 */
static int pump(int max_iter)
{
    uint8_t buf[MAX_FRAME_SIZE];
    int iter;

    for (iter = 0; iter < max_iter; iter++)
    {
        size_t blen = pouch_serial_broker_frame_get(test_broker, buf, sizeof(buf));
        if (blen > 0)
        {
            int err = pouch_serial_device_recv(buf, blen);
            zassert_ok(err, "device_recv failed: %d", err);
            continue;
        }

        size_t dlen = pouch_serial_device_frame_get(buf, sizeof(buf));
        if (dlen > 0)
        {
            int err = pouch_serial_broker_recv(test_broker, buf, dlen);
            zassert_ok(err, "broker_recv failed: %d", err);
            continue;
        }

        break;
    }

    zassert_true(iter < max_iter, "exchange did not converge within %d iterations", max_iter);
    return iter;
}

/*
 * Pump variant that tolerates recv errors and stops as soon as the broker
 * signals completion (success or failure).  Used by error-injection tests
 * where one side's recv may legitimately return a negative error code.
 */
static int pump_until_done(int max_iter)
{
    uint8_t buf[MAX_FRAME_SIZE];
    int iter;

    for (iter = 0; iter < max_iter; iter++)
    {
        if (broker_done)
        {
            break;
        }

        size_t blen = pouch_serial_broker_frame_get(test_broker, buf, sizeof(buf));
        if (blen > 0)
        {
            (void) pouch_serial_device_recv(buf, blen);
            continue;
        }

        size_t dlen = pouch_serial_device_frame_get(buf, sizeof(buf));
        if (dlen > 0)
        {
            (void) pouch_serial_broker_recv(test_broker, buf, dlen);
            continue;
        }

        break;
    }

    zassert_true(iter < max_iter, "exchange did not converge within %d iterations", max_iter);
    return iter;
}

static void run_exchange(void)
{
    broker_done = false;
    broker_success = false;

    pouch_serial_broker_start(test_broker);

    pump(MAX_PUMP_ITER);

    zassert_true(broker_done, "exchange did not complete");
    zassert_true(broker_success, "exchange ended with failure");
}

static void *suite_setup(void)
{
    test_broker = pouch_serial_broker_create(&adapter);
    zassert_not_null(test_broker, "broker_create returned NULL");

    pouch_serial_device_init(device_ready);

    return NULL;
}

static void before(void *f)
{
    (void) f;

    pouch_serial_broker_destroy(test_broker);
    test_broker = pouch_serial_broker_create(&adapter);
    zassert_not_null(test_broker, "broker_create returned NULL");

    stubs_reset();
    pouch_serial_device_init(device_ready);

    broker_done = false;
    broker_success = false;
}

ZTEST_SUITE(serial_exchange, NULL, suite_setup, before, NULL, NULL);

static const uint8_t info_payload[] = "device-info-stub";
static const uint8_t server_cert_payload[] = "server-certificate-data";
static const uint8_t device_cert_payload[] = "device-certificate-data";
static const uint8_t downlink_payload[] = "downlink-payload";
static const uint8_t uplink_payload[] = "uplink-payload";

static void load_payloads(const uint8_t *info,
                          size_t info_len,
                          const uint8_t *server_cert,
                          size_t server_cert_len,
                          const uint8_t *device_cert,
                          size_t device_cert_len,
                          const uint8_t *downlink,
                          size_t downlink_len,
                          const uint8_t *uplink,
                          size_t uplink_len)
{
    stub_sender_set_data(&device_stubs.info, info, info_len);
    stub_sender_set_data(&broker_stubs.server_cert, server_cert, server_cert_len);
    stub_sender_set_data(&device_stubs.device_cert, device_cert, device_cert_len);
    stub_sender_set_data(&broker_stubs.downlink, downlink, downlink_len);
    stub_sender_set_data(&device_stubs.uplink, uplink, uplink_len);
}

static void load_default_payloads(void)
{
    load_payloads(info_payload,
                  sizeof(info_payload),
                  server_cert_payload,
                  sizeof(server_cert_payload),
                  device_cert_payload,
                  sizeof(device_cert_payload),
                  downlink_payload,
                  sizeof(downlink_payload),
                  uplink_payload,
                  sizeof(uplink_payload));
}

ZTEST(serial_exchange, test_single_fragment)
{
    load_default_payloads();
    run_exchange();

    /* Broker received correct data from device. */
    zassert_equal(broker_stubs.info.rx_len, sizeof(info_payload));
    zassert_mem_equal(broker_stubs.info.rx_buf, info_payload, sizeof(info_payload));

    zassert_equal(broker_stubs.device_cert.rx_len, sizeof(device_cert_payload));
    zassert_mem_equal(broker_stubs.device_cert.rx_buf,
                      device_cert_payload,
                      sizeof(device_cert_payload));

    zassert_equal(broker_stubs.uplink.rx_len, sizeof(uplink_payload));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, uplink_payload, sizeof(uplink_payload));

    /* Device received correct data from broker. */
    zassert_equal(device_stubs.server_cert.rx_len, sizeof(server_cert_payload));
    zassert_mem_equal(device_stubs.server_cert.rx_buf,
                      server_cert_payload,
                      sizeof(server_cert_payload));

    zassert_equal(device_stubs.downlink.rx_len, sizeof(downlink_payload));
    zassert_mem_equal(device_stubs.downlink.rx_buf, downlink_payload, sizeof(downlink_payload));
}

#define LARGE_PAYLOAD_SIZE 200

static void fill_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
    for (size_t i = 0; i < len; i++)
    {
        buf[i] = (uint8_t) (seed + i);
    }
}

ZTEST(serial_exchange, test_multi_fragment)
{
    uint8_t info_large[LARGE_PAYLOAD_SIZE];
    uint8_t server_cert_large[LARGE_PAYLOAD_SIZE];
    uint8_t device_cert_large[LARGE_PAYLOAD_SIZE];
    uint8_t downlink_large[LARGE_PAYLOAD_SIZE];
    uint8_t uplink_large[LARGE_PAYLOAD_SIZE];

    fill_pattern(info_large, sizeof(info_large), 0x10);
    fill_pattern(server_cert_large, sizeof(server_cert_large), 0x20);
    fill_pattern(device_cert_large, sizeof(device_cert_large), 0x30);
    fill_pattern(downlink_large, sizeof(downlink_large), 0x40);
    fill_pattern(uplink_large, sizeof(uplink_large), 0x50);

    load_payloads(info_large,
                  sizeof(info_large),
                  server_cert_large,
                  sizeof(server_cert_large),
                  device_cert_large,
                  sizeof(device_cert_large),
                  downlink_large,
                  sizeof(downlink_large),
                  uplink_large,
                  sizeof(uplink_large));

    run_exchange();

    zassert_equal(broker_stubs.info.rx_len, sizeof(info_large));
    zassert_mem_equal(broker_stubs.info.rx_buf, info_large, sizeof(info_large));

    zassert_equal(broker_stubs.device_cert.rx_len, sizeof(device_cert_large));
    zassert_mem_equal(broker_stubs.device_cert.rx_buf,
                      device_cert_large,
                      sizeof(device_cert_large));

    zassert_equal(broker_stubs.uplink.rx_len, sizeof(uplink_large));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, uplink_large, sizeof(uplink_large));

    zassert_equal(device_stubs.server_cert.rx_len, sizeof(server_cert_large));
    zassert_mem_equal(device_stubs.server_cert.rx_buf,
                      server_cert_large,
                      sizeof(server_cert_large));

    zassert_equal(device_stubs.downlink.rx_len, sizeof(downlink_large));
    zassert_mem_equal(device_stubs.downlink.rx_buf, downlink_large, sizeof(downlink_large));
}

ZTEST(serial_exchange, test_empty_senders)
{
    /* All sender tx_len fields are zero after stubs_reset(). */
    run_exchange();

    zassert_equal(broker_stubs.info.rx_len, 0);
    zassert_equal(broker_stubs.device_cert.rx_len, 0);
    zassert_equal(broker_stubs.uplink.rx_len, 0);
    zassert_equal(device_stubs.server_cert.rx_len, 0);
    zassert_equal(device_stubs.downlink.rx_len, 0);
}

ZTEST(serial_exchange, test_endpoint_lifecycle)
{
    load_default_payloads();
    run_exchange();

    /* Every broker endpoint started and ended successfully exactly once. */
    zassert_equal(broker_stubs.info.start_count, 1);
    zassert_equal(broker_stubs.info.end_success_count, 1);
    zassert_equal(broker_stubs.info.end_fail_count, 0);

    zassert_equal(broker_stubs.server_cert.start_count, 1);
    zassert_equal(broker_stubs.server_cert.end_success_count, 1);
    zassert_equal(broker_stubs.server_cert.end_fail_count, 0);

    zassert_equal(broker_stubs.device_cert.start_count, 1);
    zassert_equal(broker_stubs.device_cert.end_success_count, 1);
    zassert_equal(broker_stubs.device_cert.end_fail_count, 0);

    zassert_equal(broker_stubs.downlink.start_count, 1);
    zassert_equal(broker_stubs.downlink.end_success_count, 1);
    zassert_equal(broker_stubs.downlink.end_fail_count, 0);

    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 1);
    zassert_equal(broker_stubs.uplink.end_fail_count, 0);

    /* Every device endpoint started and ended successfully exactly once. */
    zassert_equal(device_stubs.info.start_count, 1);
    zassert_equal(device_stubs.info.end_success_count, 1);
    zassert_equal(device_stubs.info.end_fail_count, 0);

    zassert_equal(device_stubs.server_cert.start_count, 1);
    zassert_equal(device_stubs.server_cert.end_success_count, 1);
    zassert_equal(device_stubs.server_cert.end_fail_count, 0);

    zassert_equal(device_stubs.device_cert.start_count, 1);
    zassert_equal(device_stubs.device_cert.end_success_count, 1);
    zassert_equal(device_stubs.device_cert.end_fail_count, 0);

    zassert_equal(device_stubs.downlink.start_count, 1);
    zassert_equal(device_stubs.downlink.end_success_count, 1);
    zassert_equal(device_stubs.downlink.end_fail_count, 0);

    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_success_count, 1);
    zassert_equal(device_stubs.uplink.end_fail_count, 0);
}

ZTEST(serial_exchange, test_asymmetric_payloads)
{
    uint8_t info_small[10];
    uint8_t server_cert_large[LARGE_PAYLOAD_SIZE];
    uint8_t device_cert_medium[50];
    uint8_t uplink_large[150];

    fill_pattern(info_small, sizeof(info_small), 0xA0);
    fill_pattern(server_cert_large, sizeof(server_cert_large), 0xB0);
    fill_pattern(device_cert_medium, sizeof(device_cert_medium), 0xC0);
    fill_pattern(uplink_large, sizeof(uplink_large), 0xD0);

    /* downlink is empty (zero-length), all others have varying sizes. */
    stub_sender_set_data(&device_stubs.info, info_small, sizeof(info_small));
    stub_sender_set_data(&broker_stubs.server_cert, server_cert_large, sizeof(server_cert_large));
    stub_sender_set_data(&device_stubs.device_cert, device_cert_medium, sizeof(device_cert_medium));
    /* broker_stubs.downlink left at zero length. */
    stub_sender_set_data(&device_stubs.uplink, uplink_large, sizeof(uplink_large));

    run_exchange();

    zassert_equal(broker_stubs.info.rx_len, sizeof(info_small));
    zassert_mem_equal(broker_stubs.info.rx_buf, info_small, sizeof(info_small));

    zassert_equal(device_stubs.server_cert.rx_len, sizeof(server_cert_large));
    zassert_mem_equal(device_stubs.server_cert.rx_buf,
                      server_cert_large,
                      sizeof(server_cert_large));

    zassert_equal(broker_stubs.device_cert.rx_len, sizeof(device_cert_medium));
    zassert_mem_equal(broker_stubs.device_cert.rx_buf,
                      device_cert_medium,
                      sizeof(device_cert_medium));

    zassert_equal(device_stubs.downlink.rx_len, 0);

    zassert_equal(broker_stubs.uplink.rx_len, sizeof(uplink_large));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, uplink_large, sizeof(uplink_large));
}

/*
 * Broker receiver recv error: the broker's UPLINK recv callback returns an
 * error.  The broker should close the channel with failure and signal
 * adapter->end(false).
 */
ZTEST(serial_exchange, test_recv_error)
{
    load_default_payloads();
    broker_stubs.uplink.recv_err = -EIO;

    pouch_serial_broker_start(test_broker);

    pump_until_done(MAX_PUMP_ITER);

    zassert_true(broker_done, "exchange did not complete");
    zassert_false(broker_success, "exchange should have failed");

    /* The broker uplink receiver was started but ended with failure. */
    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_fail_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 0);
}

/*
 * Broker sender send error: the broker's DOWNLINK send callback returns
 * POUCH_ERROR on the first send() call.  The error DATA frame is produced,
 * ch_close fires, and channel_closed terminates the exchange with failure.
 */
ZTEST(serial_exchange, test_send_error)
{
    load_default_payloads();
    broker_stubs.downlink.send_err_after = 0;

    pouch_serial_broker_start(test_broker);

    pump_until_done(MAX_PUMP_ITER);

    zassert_true(broker_done, "exchange did not complete");
    zassert_false(broker_success, "exchange should have failed");

    /* The broker downlink sender was started but ended with failure. */
    zassert_equal(broker_stubs.downlink.start_count, 1);
    zassert_equal(broker_stubs.downlink.end_fail_count, 1);
    zassert_equal(broker_stubs.downlink.end_success_count, 0);
}

/*
 * Device sender send error propagating to broker: the device's UPLINK send
 * callback returns POUCH_ERROR after one successful send.  The error DATA
 * reaches the broker's already-open UPLINK receiver, which closes with
 * failure and terminates the exchange.
 *
 * Requires a multi-fragment uplink payload so the first DATA succeeds (opening
 * the broker receiver) and the second DATA carries the error.
 */
ZTEST(serial_exchange, test_remote_send_error)
{
    uint8_t uplink_large[LARGE_PAYLOAD_SIZE];
    fill_pattern(uplink_large, sizeof(uplink_large), 0xE0);

    load_default_payloads();
    stub_sender_set_data(&device_stubs.uplink, uplink_large, sizeof(uplink_large));
    device_stubs.uplink.send_err_after = 1;

    pouch_serial_broker_start(test_broker);

    pump_until_done(MAX_PUMP_ITER);

    zassert_true(broker_done, "exchange did not complete");
    zassert_false(broker_success, "exchange should have failed");

    /* Both the device sender and broker receiver ended with failure. */
    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_fail_count, 1);
    zassert_equal(device_stubs.uplink.end_success_count, 0);

    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_fail_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 0);
}
