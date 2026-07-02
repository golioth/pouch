/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <string.h>
#include <errno.h>

#include <pouch/transport/serial/broker.h>
#include <pouch/transport/serial/common.h>
#include "transport/bearer.h"
#include "transport/serial/packet.h"
#include "gateway/types.h"
#include "stub_endpoints.h"

#define FRAME_BUF_SIZE 64
#define MAX_FRAMES 200

/* ---- Adapter mock -------------------------------------------------------- */

static int ready_count;
static int end_count;
static bool end_success;
static struct pouch_serial_broker *test_broker;

static void adapter_ready(const struct pouch_serial_broker *broker)
{
    (void) broker;
    ready_count++;
}

static void adapter_end(const struct pouch_serial_broker *broker, bool success)
{
    (void) broker;
    end_count++;
    end_success = success;
}

static const struct pouch_serial_broker_adapter test_adapter = {
    .ready = adapter_ready,
    .end = adapter_end,
};

/* ---- Frame helpers ------------------------------------------------------- */

static size_t build_frame(uint8_t *buf,
                          size_t bufsize,
                          const struct pouch_serial_header *hdr,
                          const void *payload,
                          size_t payload_len)
{
    zassert_true(POUCH_SERIAL_HEADER_LEN + payload_len <= bufsize);
    buf[0] = pouch_serial_header_encode(hdr);

    if (payload_len > 0)
    {
        memcpy(&buf[POUCH_SERIAL_HEADER_LEN], payload, payload_len);
    }

    return POUCH_SERIAL_HEADER_LEN + payload_len;
}

/** Send a frame to the broker (simulating device -> broker). */
static int send_frame(const struct pouch_serial_header *hdr,
                      const void *payload,
                      size_t payload_len)
{
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = build_frame(buf, sizeof(buf), hdr, payload, payload_len);

    return pouch_serial_broker_recv(test_broker, buf, len);
}

/** Send an ACK frame to the broker. */
static int send_ack(uint8_t channel, bool err)
{
    struct pouch_serial_header hdr = {
        .is_data = false,
        .err = err,
        .channel = channel,
    };

    return send_frame(&hdr, NULL, 0);
}

/** Send a DATA frame to the broker (simulating device sending data). */
static int send_data(uint8_t channel,
                     bool first,
                     bool last,
                     bool err,
                     const void *payload,
                     size_t payload_len)
{
    struct pouch_serial_header hdr = {
        .is_data = true,
        .first = first,
        .last = last,
        .err = err,
        .channel = channel,
    };

    return send_frame(&hdr, payload, payload_len);
}

/**
 * Get a frame from the broker and decode its header.
 *
 * @return Total frame length, or 0 if no frame was available.
 */
static size_t get_frame(struct pouch_serial_header *hdr,
                        const uint8_t **payload,
                        size_t *payload_len,
                        uint8_t *buf,
                        size_t bufsize)
{
    size_t len = pouch_serial_broker_frame_get(test_broker, buf, bufsize);
    if (len == 0)
    {
        return 0;
    }

    zassert_true(len >= POUCH_SERIAL_HEADER_LEN, "frame too short: %zu", len);
    int err = pouch_serial_header_decode(buf[0], hdr);
    zassert_ok(err, "failed to decode header: %d", err);

    if (len > POUCH_SERIAL_HEADER_LEN)
    {
        *payload = &buf[POUCH_SERIAL_HEADER_LEN];
        *payload_len = len - POUCH_SERIAL_HEADER_LEN;
    }
    else
    {
        *payload = NULL;
        *payload_len = 0;
    }

    return len;
}

/**
 * Drain a broker sender channel: get the prompt ACK, send ACK to open it,
 * then collect all DATA frames.
 *
 * The serial protocol requires the sender to produce a prompt ACK when it has
 * data to send. The remote side responds with an ACK to open the channel,
 * after which the sender produces DATA frames.
 */
static void drain_sender_channel(uint8_t channel,
                                 uint8_t *out,
                                 size_t *out_len,
                                 size_t out_size,
                                 int *frame_count,
                                 bool *saw_first,
                                 bool *saw_last,
                                 bool *saw_err)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    *out_len = 0;
    *frame_count = 0;
    *saw_first = false;
    *saw_last = false;
    *saw_err = false;

    /* Get the prompt ACK from the sender */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected prompt ACK for channel %u", channel);
    zassert_equal(hdr.channel,
                  channel,
                  "unexpected channel %u (expected %u)",
                  hdr.channel,
                  channel);
    zassert_false(hdr.is_data, "expected ACK prompt, got DATA");
    zassert_false(hdr.err, "unexpected ERR on prompt ACK");

    /* Send ACK to open the sender channel */
    zassert_ok(send_ack(channel, false));

    for (int i = 0; i < MAX_FRAMES; i++)
    {
        len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        if (len == 0)
        {
            break;
        }

        zassert_equal(hdr.channel,
                      channel,
                      "unexpected channel %u (expected %u)",
                      hdr.channel,
                      channel);
        zassert_true(hdr.is_data, "expected DATA frame, got ACK");

        if (hdr.first)
        {
            *saw_first = true;
        }
        if (hdr.err)
        {
            *saw_err = true;
        }

        if (payload_len > 0)
        {
            zassert_true(*out_len + payload_len <= out_size, "output buffer overflow");
            memcpy(&out[*out_len], payload, payload_len);
            *out_len += payload_len;
        }

        (*frame_count)++;

        if (hdr.last)
        {
            *saw_last = true;
            break;
        }
    }
}

/**
 * Complete a receiver channel exchange: get the broker's ACK prompt,
 * then send DATA frames with the given payload.
 */
static void complete_receiver_channel(uint8_t channel, const void *data, size_t data_len)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get the ACK prompt from the broker */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected ACK prompt for channel %u", channel);
    zassert_equal(hdr.channel, channel);
    zassert_false(hdr.is_data, "expected ACK prompt, got DATA");
    zassert_false(hdr.err);

    /* Send DATA response (first+last, with optional payload) */
    zassert_ok(send_data(channel, true, true, false, data, data_len));
}

/**
 * Complete a sender channel exchange: drain all DATA frames from the broker,
 * ACKing each one.
 */
static void complete_sender_channel(uint8_t channel)
{
    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;

    drain_sender_channel(channel,
                         out,
                         &out_len,
                         sizeof(out),
                         &frame_count,
                         &saw_first,
                         &saw_last,
                         &saw_err);
}

/**
 * Run the broker sequence from start through all channels up to (but not
 * including) the sync phase.
 *
 * The provisioning flags must be set BEFORE the INFO channel closes, because
 * the broker's next() function checks them immediately when INFO completes.
 * To achieve this, we send INFO data as two fragments: the first fragment
 * opens the channel and captures the bearer (giving us access to the node),
 * then we set the flags, then the second fragment closes INFO.
 *
 * After this call, both UPLINK and DOWNLINK channels are active.
 */
static void advance_to_sync(bool skip_server_cert, bool skip_device_cert)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    pouch_serial_broker_start(test_broker);

    /* 1. INFO: get the ACK prompt */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected INFO prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_INFO);
    zassert_false(hdr.is_data);

    /* Send first DATA fragment (opens the channel, captures bearer) */
    zassert_ok(send_data(POUCH_SERIAL_CH_INFO, true, false, false, NULL, 0));

    /* Now set provisioning flags BEFORE closing INFO */
    struct pouch_gateway_node_info *node = broker_stubs.info.bearer->ctx;
    zassert_not_null(node);
    node->server_cert_provisioned = skip_server_cert;
    node->device_cert_provisioned = skip_device_cert;

    /* Send last DATA fragment to close INFO: triggers next() */
    zassert_ok(send_data(POUCH_SERIAL_CH_INFO, false, true, false, NULL, 0));

    /* 2. SERVER_CERT (if not provisioned): broker sends data.
     * Get prompt ACK, open the channel, then drain DATA frames. */
    if (!skip_server_cert)
    {
        /* Get prompt ACK from the SERVER_CERT sender */
        len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        zassert_true(len > 0, "expected SERVER_CERT prompt ACK");
        zassert_equal(hdr.channel, POUCH_SERIAL_CH_SERVER_CERT);
        zassert_false(hdr.is_data);

        /* Mark as provisioned before opening (ch_close triggers next()) */
        node->server_cert_provisioned = true;

        /* Send ACK to open the sender channel */
        zassert_ok(send_ack(POUCH_SERIAL_CH_SERVER_CERT, false));

        /* Drain all DATA frames */
        for (int i = 0; i < MAX_FRAMES; i++)
        {
            len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
            if (len == 0)
            {
                break;
            }
            zassert_equal(hdr.channel, POUCH_SERIAL_CH_SERVER_CERT);
            zassert_true(hdr.is_data);
            if (hdr.last)
            {
                break;
            }
        }
    }

    /* 3. DEVICE_CERT (if not provisioned): broker prompts, we send data.
     * Must set device_cert_provisioned BEFORE the channel closes. */
    if (!skip_device_cert)
    {
        /* Get prompt ACK */
        len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        zassert_true(len > 0, "expected DEVICE_CERT prompt");
        zassert_equal(hdr.channel, POUCH_SERIAL_CH_DEVICE_CERT);
        zassert_false(hdr.is_data);

        /* Mark as provisioned before sending last DATA (ch_close triggers next()) */
        node->device_cert_provisioned = true;

        /* Send DATA (first+last): closing triggers next() */
        zassert_ok(send_data(POUCH_SERIAL_CH_DEVICE_CERT, true, true, false, NULL, 0));
    }

    /* Now broker enters sync: both UPLINK and DOWNLINK are activated */
}

/**
 * Complete the sync phase: handle DOWNLINK (sender) and UPLINK (receiver)
 * frames until both are done.
 *
 * DOWNLINK is a sender channel: we send ACK to open it, then drain DATA.
 * UPLINK is a receiver channel: broker sends ACK prompt, we send DATA back.
 */
static void complete_sync(void)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    bool downlink_done = false;
    bool uplink_done = false;

    for (int i = 0; i < MAX_FRAMES && !(downlink_done && uplink_done); i++)
    {
        size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        if (len == 0)
        {
            break;
        }

        if (hdr.channel == POUCH_SERIAL_CH_DOWNLINK)
        {
            if (!hdr.is_data)
            {
                /* Prompt ACK: open the sender channel */
                send_ack(POUCH_SERIAL_CH_DOWNLINK, false);
            }
            else if (hdr.last)
            {
                downlink_done = true;
            }
        }
        else if (hdr.channel == POUCH_SERIAL_CH_UPLINK)
        {
            zassert_false(hdr.is_data, "expected ACK on UPLINK");
            /* Send empty DATA response */
            send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, NULL, 0);
            uplink_done = true;
        }
    }

    zassert_true(downlink_done, "downlink did not complete");
    zassert_true(uplink_done, "uplink did not complete");
}

/**
 * Run a full exchange to completion with both certs provisioned and no data.
 */
static void run_empty_exchange(void)
{
    advance_to_sync(true, true);
    complete_sync();
    zassert_equal(end_count, 1, "expected end callback");
    zassert_true(end_success, "expected successful end");
}

/* ---- Reset helpers ------------------------------------------------------- */

static void reset_stubs(void)
{
    stub_receiver_reset(&broker_stubs.info);
    stub_sender_reset(&broker_stubs.server_cert);
    stub_receiver_reset(&broker_stubs.device_cert);
    stub_sender_reset(&broker_stubs.downlink);
    stub_receiver_reset(&broker_stubs.uplink);
}

static void reset_all(void)
{
    reset_stubs();
    ready_count = 0;
    end_count = 0;
    end_success = false;
}

/* ---- Test fixture -------------------------------------------------------- */

static void *suite_setup(void)
{
    test_broker = pouch_serial_broker_create(&test_adapter);
    zassert_not_null(test_broker);
    return NULL;
}

static void before(void *f)
{
    (void) f;
    if (test_broker != NULL)
    {
        pouch_serial_broker_destroy(test_broker);
    }
    test_broker = pouch_serial_broker_create(&test_adapter);
    zassert_not_null(test_broker);
    reset_all();
}

ZTEST_SUITE(serial_broker, NULL, suite_setup, before, NULL, NULL);

/* ==========================================================================
 * Broker lifecycle tests
 * ========================================================================== */

ZTEST(serial_broker, test_create_null_adapter)
{
    struct pouch_serial_broker *b = pouch_serial_broker_create(NULL);
    zassert_is_null(b);
}

ZTEST(serial_broker, test_recv_null_params)
{
    uint8_t buf[4] = {0};
    zassert_equal(pouch_serial_broker_recv(NULL, buf, 1), -EINVAL);
    zassert_equal(pouch_serial_broker_recv(test_broker, NULL, 1), -EINVAL);
    zassert_equal(pouch_serial_broker_recv(test_broker, buf, 0), -EINVAL);
}

ZTEST(serial_broker, test_frame_get_null_params)
{
    uint8_t buf[FRAME_BUF_SIZE];
    zassert_equal(pouch_serial_broker_frame_get(NULL, buf, sizeof(buf)), 0);
    zassert_equal(pouch_serial_broker_frame_get(test_broker, NULL, sizeof(buf)), 0);
}

ZTEST(serial_broker, test_adapter_get)
{
    zassert_equal_ptr(pouch_serial_broker_adapter_get(test_broker), &test_adapter);
}

ZTEST(serial_broker, test_adapter_get_null)
{
    zassert_is_null(pouch_serial_broker_adapter_get(NULL));
}

ZTEST(serial_broker, test_frame_get_no_pending)
{
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = pouch_serial_broker_frame_get(test_broker, buf, sizeof(buf));
    zassert_equal(len, 0, "expected no frames from idle broker");
}

ZTEST(serial_broker, test_start_triggers_info_prompt)
{
    pouch_serial_broker_start(test_broker);
    zassert_true(ready_count > 0, "ready callback not called after start");

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected frame after start");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_INFO);
    zassert_false(hdr.is_data, "expected ACK prompt for INFO");
}

/* ==========================================================================
 * Full exchange sequence tests
 * ========================================================================== */

ZTEST(serial_broker, test_full_exchange_all_provisioned)
{
    run_empty_exchange();

    zassert_equal(broker_stubs.info.start_count, 1);
    zassert_equal(broker_stubs.info.end_success_count, 1);
    zassert_equal(broker_stubs.server_cert.start_count, 0);
    zassert_equal(broker_stubs.device_cert.start_count, 0);
    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.downlink.start_count, 1);
}

ZTEST(serial_broker, test_full_exchange_needs_server_cert)
{
    static const uint8_t cert[] = {0xDE, 0xAD, 0xBE, 0xEF};
    stub_sender_set_data(&broker_stubs.server_cert, cert, sizeof(cert));

    advance_to_sync(false, true);
    complete_sync();

    zassert_equal(end_count, 1);
    zassert_true(end_success);
    zassert_equal(broker_stubs.server_cert.start_count, 1);
    zassert_equal(broker_stubs.server_cert.end_success_count, 1);
}

ZTEST(serial_broker, test_full_exchange_needs_device_cert)
{
    advance_to_sync(true, false);
    complete_sync();

    zassert_equal(end_count, 1);
    zassert_true(end_success);
    zassert_equal(broker_stubs.device_cert.start_count, 1);
    zassert_equal(broker_stubs.device_cert.end_success_count, 1);
}

ZTEST(serial_broker, test_full_exchange_needs_both_certs)
{
    advance_to_sync(false, false);
    complete_sync();

    zassert_equal(end_count, 1);
    zassert_true(end_success);
    zassert_equal(broker_stubs.server_cert.start_count, 1);
    zassert_equal(broker_stubs.device_cert.start_count, 1);
}

ZTEST(serial_broker, test_consecutive_exchanges)
{
    for (int i = 0; i < 2; i++)
    {
        /* Recreate broker to get clean channel state */
        pouch_serial_broker_destroy(test_broker);
        test_broker = pouch_serial_broker_create(&test_adapter);
        zassert_not_null(test_broker);
        reset_all();

        advance_to_sync(true, true);
        complete_sync();
        zassert_equal(end_count, 1, "exchange %d: end not called", i);
        zassert_true(end_success, "exchange %d: failed", i);
    }
}

/* ==========================================================================
 * Sender channel tests (SERVER_CERT=1, DOWNLINK=3)
 *
 * For sender channels, the broker sends DATA frames and the device (test)
 * responds with ACK frames.
 * ========================================================================== */

ZTEST(serial_broker, test_sender_single_fragment)
{
    static const uint8_t payload[] = {0x10, 0x20, 0x30};
    stub_sender_set_data(&broker_stubs.downlink, payload, sizeof(payload));

    advance_to_sync(true, true);

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;

    drain_sender_channel(POUCH_SERIAL_CH_DOWNLINK,
                         out,
                         &out_len,
                         sizeof(out),
                         &frame_count,
                         &saw_first,
                         &saw_last,
                         &saw_err);

    zassert_true(saw_first, "missing FIRST flag");
    zassert_true(saw_last, "missing LAST flag");
    zassert_false(saw_err, "unexpected ERR flag");
    zassert_equal(out_len, sizeof(payload));
    zassert_mem_equal(out, payload, sizeof(payload));
    zassert_equal(broker_stubs.downlink.start_count, 1);
    zassert_equal(broker_stubs.downlink.end_success_count, 1);
}

ZTEST(serial_broker, test_sender_multi_fragment)
{
    /* Create payload larger than frame buffer to force multi-frame transfer */
    uint8_t payload[FRAME_BUF_SIZE * 2];
    memset(payload, 0xAB, sizeof(payload));
    stub_sender_set_data(&broker_stubs.downlink, payload, sizeof(payload));

    advance_to_sync(true, true);

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;

    drain_sender_channel(POUCH_SERIAL_CH_DOWNLINK,
                         out,
                         &out_len,
                         sizeof(out),
                         &frame_count,
                         &saw_first,
                         &saw_last,
                         &saw_err);

    zassert_true(saw_first);
    zassert_true(saw_last);
    zassert_false(saw_err);
    zassert_true(frame_count > 1, "expected multiple frames for large payload");
    zassert_equal(out_len, sizeof(payload));
    zassert_mem_equal(out, payload, sizeof(payload));
}

ZTEST(serial_broker, test_sender_empty_transfer)
{
    /* No data loaded on DOWNLINK sender */
    advance_to_sync(true, true);

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;

    drain_sender_channel(POUCH_SERIAL_CH_DOWNLINK,
                         out,
                         &out_len,
                         sizeof(out),
                         &frame_count,
                         &saw_first,
                         &saw_last,
                         &saw_err);

    zassert_true(saw_first);
    zassert_true(saw_last);
    zassert_false(saw_err);
    zassert_equal(out_len, 0);
    zassert_equal(broker_stubs.downlink.start_count, 1);
    zassert_equal(broker_stubs.downlink.end_success_count, 1);
}

ZTEST(serial_broker, test_sender_ack_err_aborts)
{
    /* Use a payload large enough to require multiple frames so that
     * the first DATA frame is not also the last. */
    uint8_t payload[FRAME_BUF_SIZE * 2];
    memset(payload, 0xAB, sizeof(payload));
    stub_sender_set_data(&broker_stubs.downlink, payload, sizeof(payload));

    advance_to_sync(true, true);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;

    /* Get DOWNLINK prompt ACK */
    size_t len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected DOWNLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_false(hdr.is_data);

    /* Open the DOWNLINK sender channel with an ACK */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, false));

    /* Get the first DATA frame from DOWNLINK */
    len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_true(hdr.first);
    zassert_false(hdr.last, "expected multi-fragment transfer");

    /* Send ACK+ERR to abort */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, true));

    zassert_equal(broker_stubs.downlink.end_fail_count, 1);
    zassert_equal(broker_stubs.downlink.end_success_count, 0);
}

ZTEST(serial_broker, test_sender_ack_err_during_multi_fragment)
{
    uint8_t payload[FRAME_BUF_SIZE * 2];
    memset(payload, 0xAB, sizeof(payload));
    stub_sender_set_data(&broker_stubs.downlink, payload, sizeof(payload));

    advance_to_sync(true, true);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;

    /* Get DOWNLINK prompt ACK */
    size_t len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected DOWNLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_false(hdr.is_data);

    /* Open the DOWNLINK sender channel with an ACK */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, false));

    /* Get first DATA frame */
    len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_true(hdr.first);
    zassert_false(hdr.last, "expected multi-fragment");

    /* Get second DATA frame (no intermediate ACK needed) */
    len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0);

    /* Now send ACK+ERR to abort mid-transfer */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, true));

    zassert_equal(broker_stubs.downlink.end_fail_count, 1);
    zassert_equal(broker_stubs.downlink.end_success_count, 0);
}

ZTEST(serial_broker, test_sender_start_error)
{
    broker_stubs.downlink.start_err = -ENOMEM;

    advance_to_sync(true, true);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get DOWNLINK prompt ACK */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected DOWNLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_false(hdr.is_data);

    /* Send ACK to open the channel: start will fail */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, false));

    /* Broker should produce an error DATA frame for DOWNLINK */
    len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected error frame");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_true(hdr.is_data);
    zassert_true(hdr.err);
    zassert_true(hdr.last);

    zassert_equal(broker_stubs.downlink.start_count, 1);
    zassert_equal(broker_stubs.downlink.end_fail_count, 1);
}

ZTEST(serial_broker, test_sender_send_error)
{
    broker_stubs.downlink.send_err = true;

    advance_to_sync(true, true);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get DOWNLINK prompt ACK */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected DOWNLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_false(hdr.is_data);

    /* Send ACK to open the channel */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, false));

    /* Broker should produce an error DATA frame */
    len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected error frame");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_true(hdr.is_data);
    zassert_true(hdr.err);
    zassert_true(hdr.last);
}

ZTEST(serial_broker, test_sender_server_cert_with_data)
{
    static const uint8_t cert[] = {0xCA, 0xFE, 0xBA, 0xBE, 0x01, 0x02};
    stub_sender_set_data(&broker_stubs.server_cert, cert, sizeof(cert));

    pouch_serial_broker_start(test_broker);

    /* Complete INFO */
    complete_receiver_channel(POUCH_SERIAL_CH_INFO, NULL, 0);

    /* Set only device cert as provisioned */
    struct pouch_gateway_node_info *node = broker_stubs.info.bearer->ctx;
    node->server_cert_provisioned = false;
    node->device_cert_provisioned = true;

    /* Drain SERVER_CERT sender */
    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;

    drain_sender_channel(POUCH_SERIAL_CH_SERVER_CERT,
                         out,
                         &out_len,
                         sizeof(out),
                         &frame_count,
                         &saw_first,
                         &saw_last,
                         &saw_err);

    zassert_true(saw_first);
    zassert_true(saw_last);
    zassert_false(saw_err);
    zassert_equal(out_len, sizeof(cert));
    zassert_mem_equal(out, cert, sizeof(cert));
}

/* ==========================================================================
 * Receiver channel tests (INFO=0, DEVICE_CERT=2, UPLINK=4)
 *
 * For receiver channels, the broker sends ACK prompts and the device (test)
 * responds with DATA frames.
 * ========================================================================== */

ZTEST(serial_broker, test_receiver_single_fragment)
{
    static const uint8_t payload[] = {0xCA, 0xFE, 0xBA, 0xBE};

    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;

    /* Get UPLINK prompt */
    size_t len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected UPLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);
    zassert_false(hdr.is_data);
    zassert_false(hdr.err);

    /* Send data to UPLINK */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, payload, sizeof(payload)));

    zassert_equal(broker_stubs.uplink.rx_len, sizeof(payload));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, payload, sizeof(payload));
    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_broker, test_receiver_multi_fragment)
{
    static const uint8_t frag1[] = {0x01, 0x02};
    static const uint8_t frag2[] = {0x03, 0x04};
    static const uint8_t frag3[] = {0x05};

    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);

    /* First fragment */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, true, false, false, frag1, sizeof(frag1)));

    /* Middle fragment */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, false, false, false, frag2, sizeof(frag2)));

    /* Last fragment */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, false, true, false, frag3, sizeof(frag3)));

    uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    zassert_equal(broker_stubs.uplink.rx_len, sizeof(expected));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, expected, sizeof(expected));
    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_broker, test_receiver_empty_transfer)
{
    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);

    /* Send empty DATA (first+last, no payload) */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, NULL, 0));

    zassert_equal(broker_stubs.uplink.rx_len, 0);
    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_broker, test_receiver_data_err_aborts)
{
    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    /* Start a transfer */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, true, false, false, (uint8_t[]){0xAA}, 1));

    /* Send error frame */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, false, true, true, NULL, 0));

    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_fail_count, 1);
    zassert_equal(broker_stubs.uplink.end_success_count, 0);
}

ZTEST(serial_broker, test_receiver_missing_first_flag)
{
    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    /* Send DATA without FIRST flag to a channel that isn't open yet */
    int err = send_data(POUCH_SERIAL_CH_UPLINK, false, false, false, (uint8_t[]){0x01}, 1);
    zassert_not_equal(err, 0, "expected error for missing FIRST flag");
}

ZTEST(serial_broker, test_receiver_duplicate_first_flag)
{
    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    /* Send first DATA */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, true, false, false, (uint8_t[]){0x01}, 1));

    /* Send another FIRST: should fail */
    int err = send_data(POUCH_SERIAL_CH_UPLINK, true, false, false, (uint8_t[]){0x02}, 1);
    zassert_not_equal(err, 0, "expected error for duplicate FIRST");

    zassert_equal(broker_stubs.uplink.end_fail_count, 1);
}

ZTEST(serial_broker, test_receiver_start_error)
{
    broker_stubs.uplink.start_err = -EBUSY;

    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    /* Send DATA: start should fail, broker should NACK */
    send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, (uint8_t[]){0x01}, 1);

    /* Get the NACK */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected NACK");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);
    zassert_false(hdr.is_data);
    zassert_true(hdr.err, "expected ERR on NACK");
}

ZTEST(serial_broker, test_receiver_recv_error)
{
    broker_stubs.uplink.recv_err = -ENOMEM;

    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    /* Send DATA: recv should fail */
    int err = send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, (uint8_t[]){0xAA}, 1);
    zassert_not_equal(err, 0);

    zassert_equal(broker_stubs.uplink.start_count, 1);
    zassert_equal(broker_stubs.uplink.end_fail_count, 1);
}

ZTEST(serial_broker, test_receiver_info_with_data)
{
    static const uint8_t info_data[] = {0x01, 0x02, 0x03};

    pouch_serial_broker_start(test_broker);

    /* Get INFO prompt */
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_INFO);
    zassert_false(hdr.is_data);

    /* Send info data */
    zassert_ok(send_data(POUCH_SERIAL_CH_INFO, true, true, false, info_data, sizeof(info_data)));

    zassert_equal(broker_stubs.info.rx_len, sizeof(info_data));
    zassert_mem_equal(broker_stubs.info.rx_buf, info_data, sizeof(info_data));
    zassert_equal(broker_stubs.info.start_count, 1);
    zassert_equal(broker_stubs.info.end_success_count, 1);
}

ZTEST(serial_broker, test_receiver_device_cert_with_data)
{
    static const uint8_t cert_data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    pouch_serial_broker_start(test_broker);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get INFO prompt */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_INFO);

    /* Send first (non-last) INFO fragment to open the channel and capture bearer */
    zassert_ok(send_data(POUCH_SERIAL_CH_INFO, true, false, false, NULL, 0));

    /* Set provisioning flags BEFORE closing INFO */
    struct pouch_gateway_node_info *node = broker_stubs.info.bearer->ctx;
    node->server_cert_provisioned = true;
    node->device_cert_provisioned = false;

    /* Close INFO: triggers next() which sees device_cert not provisioned */
    zassert_ok(send_data(POUCH_SERIAL_CH_INFO, false, true, false, NULL, 0));

    /* Get DEVICE_CERT prompt */
    len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DEVICE_CERT);
    zassert_false(hdr.is_data);

    /* Send cert data */
    zassert_ok(
        send_data(POUCH_SERIAL_CH_DEVICE_CERT, true, true, false, cert_data, sizeof(cert_data)));

    zassert_equal(broker_stubs.device_cert.rx_len, sizeof(cert_data));
    zassert_mem_equal(broker_stubs.device_cert.rx_buf, cert_data, sizeof(cert_data));
    zassert_equal(broker_stubs.device_cert.start_count, 1);
    zassert_equal(broker_stubs.device_cert.end_success_count, 1);
}

/* ==========================================================================
 * Cross-channel and invalid behavior tests
 * ========================================================================== */

ZTEST(serial_broker, test_invalid_channel_id)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr = {
        .is_data = false,
        .channel = POUCH_SERIAL_CHANNEL_COUNT, /* Out of range */
    };
    size_t len = build_frame(buf, sizeof(buf), &hdr, NULL, 0);
    int err = pouch_serial_broker_recv(test_broker, buf, len);
    zassert_equal(err, -EINVAL);
}

ZTEST(serial_broker, test_malformed_ack_header)
{
    /*
     * An ACK frame with reserved bits (FIRST or LAST) set is malformed.
     * Manually encode: bit7=0 (ACK), bit5=1 (FIRST, reserved), channel=0
     */
    uint8_t frame[] = {0x20}; /* 0b00100000 = ACK with FIRST bit set */
    int err = pouch_serial_broker_recv(test_broker, frame, sizeof(frame));
    zassert_equal(err, -EINVAL);
}

ZTEST(serial_broker, test_concurrent_uplink_and_downlink)
{
    static const uint8_t dl_data[] = {0xAA, 0xBB};
    static const uint8_t ul_data[] = {0xCC, 0xDD, 0xEE};

    stub_sender_set_data(&broker_stubs.downlink, dl_data, sizeof(dl_data));

    advance_to_sync(true, true);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    uint8_t dl_out[STUB_EP_BUF_SIZE];
    size_t dl_len = 0;
    bool dl_done = false;
    bool ul_done = false;

    for (int i = 0; i < MAX_FRAMES && !(dl_done && ul_done); i++)
    {
        size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        if (len == 0)
        {
            break;
        }

        if (hdr.channel == POUCH_SERIAL_CH_DOWNLINK)
        {
            if (!hdr.is_data)
            {
                /* Prompt ACK: open the sender channel */
                send_ack(POUCH_SERIAL_CH_DOWNLINK, false);
            }
            else
            {
                if (payload_len > 0)
                {
                    memcpy(&dl_out[dl_len], payload, payload_len);
                    dl_len += payload_len;
                }
                if (hdr.last)
                {
                    dl_done = true;
                }
            }
        }
        else if (hdr.channel == POUCH_SERIAL_CH_UPLINK)
        {
            zassert_false(hdr.is_data);
            /* Send uplink data */
            send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, ul_data, sizeof(ul_data));
            ul_done = true;
        }
    }

    zassert_true(dl_done, "downlink not completed");
    zassert_true(ul_done, "uplink not completed");
    zassert_equal(dl_len, sizeof(dl_data));
    zassert_mem_equal(dl_out, dl_data, sizeof(dl_data));
    zassert_equal(broker_stubs.uplink.rx_len, sizeof(ul_data));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, ul_data, sizeof(ul_data));

    /* Both channels done: end callback should have fired */
    zassert_equal(end_count, 1);
    zassert_true(end_success);
}

ZTEST(serial_broker, test_notify_triggers_uplink)
{
    advance_to_sync(true, true);

    /* Complete DOWNLINK so only UPLINK remains */
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    /* Consume the UPLINK ACK prompt */
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected initial UPLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);

    /* Verify no more frames are pending */
    zassert_equal(pouch_serial_broker_frame_get(test_broker, buf, sizeof(buf)), 0);

    /* Notify should set UPLINK as pending, producing a new prompt */
    pouch_serial_broker_notify(test_broker);

    len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected UPLINK prompt after notify");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);
    zassert_false(hdr.is_data);
}

ZTEST(serial_broker, test_notify_null_broker)
{
    /* Should not crash */
    pouch_serial_broker_notify(NULL);
}

ZTEST(serial_broker, test_info_error_ends_exchange)
{
    broker_stubs.info.start_err = -ENOMEM;

    pouch_serial_broker_start(test_broker);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get INFO prompt */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_INFO);

    /* Send INFO data: start will fail */
    send_data(POUCH_SERIAL_CH_INFO, true, true, false, NULL, 0);

    /* Broker should produce a NACK (ACK+ERR) for the channel */
    len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected NACK frame");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_INFO);
    zassert_false(hdr.is_data);
    zassert_true(hdr.err, "expected ERR on NACK");

    /*
     * Receiver start errors don't close the channel (OPEN was never fully
     * set), so channel_closed doesn't fire and adapter->end is NOT called.
     */
    zassert_equal(end_count, 0);
    zassert_equal(broker_stubs.info.start_count, 1);
}

ZTEST(serial_broker, test_downlink_error_ends_exchange)
{
    broker_stubs.downlink.start_err = -ENOMEM;

    advance_to_sync(true, true);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get DOWNLINK prompt ACK */
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected DOWNLINK prompt");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_false(hdr.is_data);

    /* DOWNLINK is a sender channel: send ACK to open it (triggers start error) */
    zassert_ok(send_ack(POUCH_SERIAL_CH_DOWNLINK, false));

    /* Broker should produce an error DATA frame */
    len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected error frame");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_true(hdr.is_data);
    zassert_true(hdr.err);
    zassert_true(hdr.last);

    /* The exchange should have ended with failure */
    zassert_equal(end_count, 1);
    zassert_false(end_success);
}

ZTEST(serial_broker, test_uplink_with_data)
{
    static const uint8_t ul_data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    advance_to_sync(true, true);
    complete_sender_channel(POUCH_SERIAL_CH_DOWNLINK);

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    /* Get UPLINK prompt */
    get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);

    /* Send uplink data */
    zassert_ok(send_data(POUCH_SERIAL_CH_UPLINK, true, true, false, ul_data, sizeof(ul_data)));

    zassert_equal(broker_stubs.uplink.rx_len, sizeof(ul_data));
    zassert_mem_equal(broker_stubs.uplink.rx_buf, ul_data, sizeof(ul_data));

    /* Exchange should complete */
    zassert_equal(end_count, 1);
    zassert_true(end_success);
}
