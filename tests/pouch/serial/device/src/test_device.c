/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <string.h>

#include <pouch/transport/serial/device.h>
#include <pouch/transport/serial/common.h>
#include "transport/serial/packet.h"
#include "transport/bearer.h"
#include "stub_endpoints.h"

#define FRAME_BUF_SIZE 64
#define MAX_FRAMES 200

/* ---- Ready callback tracking --------------------------------------------- */

static int ready_count;

static void ready_cb(void)
{
    ready_count++;
}

/* ---- Frame helpers ------------------------------------------------------- */

/**
 * Build a raw frame from a header and optional payload.
 *
 * @return Total frame length (header + payload).
 */
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

/** Build and send a frame to the device. */
static int send_frame(const struct pouch_serial_header *hdr,
                      const void *payload,
                      size_t payload_len)
{
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = build_frame(buf, sizeof(buf), hdr, payload, payload_len);
    return pouch_serial_device_recv(buf, len);
}

/** Send an ACK frame to a channel. */
static int send_ack(uint8_t channel, bool err)
{
    struct pouch_serial_header hdr = {
        .is_data = false,
        .err = err,
        .channel = channel,
    };
    return send_frame(&hdr, NULL, 0);
}

/** Send a DATA frame to a channel. */
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
 * Get a frame from the device and decode its header.
 *
 * @param[out] hdr         Decoded header.
 * @param[out] payload     Pointer to payload within buf (may be NULL if no payload).
 * @param[out] payload_len Payload length.
 * @param      buf         Buffer for frame_get to write into.
 * @param      bufsize     Size of buf.
 *
 * @return Total frame length, or 0 if no frame was available.
 */
static size_t get_frame(struct pouch_serial_header *hdr,
                        const uint8_t **payload,
                        size_t *payload_len,
                        uint8_t *buf,
                        size_t bufsize)
{
    size_t len = pouch_serial_device_frame_get(buf, bufsize);
    if (len == 0)
    {
        return 0;
    }

    zassert_true(len >= POUCH_SERIAL_HEADER_LEN, "frame too short: %zu", len);
    int err = pouch_serial_header_decode(buf[0], hdr);
    zassert_ok(err, "failed to decode response header: %d", err);

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
 * Drain all pending frames from the device into a contiguous buffer.
 *
 * @param      channel     Only collect frames for this channel (ignore others).
 * @param[out] out         Buffer to accumulate payload data.
 * @param[out] out_len     Total accumulated payload length.
 * @param      out_size    Size of out buffer.
 * @param[out] frame_count Number of frames collected.
 * @param[out] saw_first   Whether any frame had the FIRST flag.
 * @param[out] saw_last    Whether the final frame had the LAST flag.
 * @param[out] saw_err     Whether any frame had the ERR flag.
 */
static void drain_sender_frames(uint8_t channel,
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

    for (int i = 0; i < MAX_FRAMES; i++)
    {
        size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
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

/* ---- Stub reset helper --------------------------------------------------- */

static void reset_stubs(void)
{
    stub_sender_reset(&device_stubs.info);
    stub_receiver_reset(&device_stubs.server_cert);
    stub_sender_reset(&device_stubs.device_cert);
    stub_receiver_reset(&device_stubs.downlink);
    stub_sender_reset(&device_stubs.uplink);
}

/* ---- Test fixture -------------------------------------------------------- */

static void *setup(void)
{
    pouch_serial_device_init(ready_cb);
    return NULL;
}

static void before(void *f)
{
    (void) f;
    reset_stubs();
    ready_count = 0;

    /* Re-init to clear channel state from previous test */
    pouch_serial_device_init(ready_cb);
}

ZTEST_SUITE(serial_device, NULL, setup, before, NULL, NULL);

/* ==========================================================================
 * Sender channel tests (INFO=0, DEVICE_CERT=2, UPLINK=4)
 *
 * For sender channels, the broker sends an ACK to prompt the device, and
 * the device responds with DATA frames.
 * ========================================================================== */

ZTEST(serial_device, test_sender_single_fragment)
{
    static const uint8_t payload[] = {0x10, 0x20, 0x30};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    /* Prompt the device */
    int err = send_ack(POUCH_SERIAL_CH_UPLINK, false);
    zassert_ok(err);

    /* Read response */
    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;
    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
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

    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_device, test_sender_empty_transfer)
{
    /* No data loaded: tx_len stays 0 */

    int err = send_ack(POUCH_SERIAL_CH_UPLINK, false);
    zassert_ok(err);

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;
    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
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
    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_device, test_sender_ack_err_aborts)
{
    static const uint8_t payload[] = {0xAA, 0xBB};
    stub_sender_set_data(&device_stubs.info, payload, sizeof(payload));

    /* Send ACK+ERR: should abort without starting */
    int err = send_ack(POUCH_SERIAL_CH_INFO, true);
    zassert_ok(err);

    /* Device should not produce any data frames */
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = pouch_serial_device_frame_get(buf, sizeof(buf));
    zassert_equal(len, 0, "expected no frames after ACK+ERR on closed channel");

    /* Endpoint should not have been started */
    zassert_equal(device_stubs.info.start_count, 0);
}

ZTEST(serial_device, test_sender_no_data_without_prompt)
{
    static const uint8_t payload[] = {0x01};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    /* Don't send any ACK: device should have no frames to send */
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = pouch_serial_device_frame_get(buf, sizeof(buf));
    zassert_equal(len, 0, "device produced frames without ACK prompt");
}

ZTEST(serial_device, test_sender_all_channels)
{
    static const uint8_t info_data[] = {0x01, 0x02};
    static const uint8_t cert_data[] = {0x03, 0x04, 0x05};
    static const uint8_t uplink_data[] = {0x06};

    stub_sender_set_data(&device_stubs.info, info_data, sizeof(info_data));
    stub_sender_set_data(&device_stubs.device_cert, cert_data, sizeof(cert_data));
    stub_sender_set_data(&device_stubs.uplink, uplink_data, sizeof(uplink_data));

    /* Prompt all sender channels */
    zassert_ok(send_ack(POUCH_SERIAL_CH_INFO, false));
    zassert_ok(send_ack(POUCH_SERIAL_CH_DEVICE_CERT, false));
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    /* Drain all frames: frame_get uses round-robin so we collect by channel */
    uint8_t info_out[STUB_EP_BUF_SIZE] = {0};
    uint8_t cert_out[STUB_EP_BUF_SIZE] = {0};
    uint8_t uplink_out[STUB_EP_BUF_SIZE] = {0};
    size_t info_len = 0, cert_len = 0, uplink_len = 0;

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    for (int i = 0; i < MAX_FRAMES; i++)
    {
        size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        if (len == 0)
        {
            break;
        }

        uint8_t *dst = NULL;
        size_t *dst_len = NULL;

        if (hdr.channel == POUCH_SERIAL_CH_INFO)
        {
            dst = info_out;
            dst_len = &info_len;
        }
        else if (hdr.channel == POUCH_SERIAL_CH_DEVICE_CERT)
        {
            dst = cert_out;
            dst_len = &cert_len;
        }
        else if (hdr.channel == POUCH_SERIAL_CH_UPLINK)
        {
            dst = uplink_out;
            dst_len = &uplink_len;
        }
        else
        {
            zassert_unreachable("unexpected channel %u", hdr.channel);
        }

        if (payload_len > 0)
        {
            memcpy(&dst[*dst_len], payload, payload_len);
            *dst_len += payload_len;
        }
    }

    zassert_equal(info_len, sizeof(info_data));
    zassert_mem_equal(info_out, info_data, sizeof(info_data));

    zassert_equal(cert_len, sizeof(cert_data));
    zassert_mem_equal(cert_out, cert_data, sizeof(cert_data));

    zassert_equal(uplink_len, sizeof(uplink_data));
    zassert_mem_equal(uplink_out, uplink_data, sizeof(uplink_data));
}

ZTEST(serial_device, test_sender_start_error)
{
    device_stubs.uplink.start_err = -ENOMEM;

    int err = send_ack(POUCH_SERIAL_CH_UPLINK, false);
    zassert_ok(err);

    /* Device should produce an error DATA frame */
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    zassert_true(len > 0, "expected error frame");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);
    zassert_true(hdr.is_data);
    zassert_true(hdr.err);
    zassert_true(hdr.last);

    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_fail_count, 1);
}

/* ==========================================================================
 * Receiver channel tests (SERVER_CERT=1, DOWNLINK=3)
 *
 * For receiver channels, the broker sends DATA frames to the device, and
 * the device responds with ACK frames.
 * ========================================================================== */

ZTEST(serial_device, test_receiver_single_fragment)
{
    static const uint8_t payload[] = {0xCA, 0xFE, 0xBA, 0xBE};

    int err = send_data(POUCH_SERIAL_CH_DOWNLINK, true, true, false, payload, sizeof(payload));
    zassert_ok(err);

    /* No ACK is produced for the last (or only) data frame. */

    /* Verify endpoint received the data */
    zassert_equal(device_stubs.downlink.rx_len, sizeof(payload));
    zassert_mem_equal(device_stubs.downlink.rx_buf, payload, sizeof(payload));
    zassert_equal(device_stubs.downlink.start_count, 1);
    zassert_equal(device_stubs.downlink.end_success_count, 1);
}

ZTEST(serial_device, test_receiver_multi_fragment)
{
    static const uint8_t frag1[] = {0x01, 0x02};
    static const uint8_t frag2[] = {0x03, 0x04};
    static const uint8_t frag3[] = {0x05};

    /* First fragment */
    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT, true, false, false, frag1, sizeof(frag1)));

    /* Middle fragment */
    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT, false, false, false, frag2, sizeof(frag2)));

    /* Last fragment */
    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT, false, true, false, frag3, sizeof(frag3)));

    /* No intermediate ACKs are produced; receiver processes data silently. */

    /* Verify accumulated data */
    uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    zassert_equal(device_stubs.server_cert.rx_len, sizeof(expected));
    zassert_mem_equal(device_stubs.server_cert.rx_buf, expected, sizeof(expected));
    zassert_equal(device_stubs.server_cert.start_count, 1);
    zassert_equal(device_stubs.server_cert.end_success_count, 1);
}

ZTEST(serial_device, test_receiver_empty_transfer)
{
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, true, false, NULL, 0));

    /* No ACK is produced for the last (or only) data frame. */
    zassert_equal(device_stubs.downlink.rx_len, 0);
    zassert_equal(device_stubs.downlink.start_count, 1);
    zassert_equal(device_stubs.downlink.end_success_count, 1);
}

ZTEST(serial_device, test_receiver_data_err_aborts)
{
    static const uint8_t frag1[] = {0xAA};

    /* Start a transfer */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, false, false, frag1, sizeof(frag1)));

    /* Send error frame */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, false, true, true, NULL, 0));

    /* Drain NACK response */
    uint8_t buf[FRAME_BUF_SIZE];
    pouch_serial_device_frame_get(buf, sizeof(buf));

    zassert_equal(device_stubs.downlink.start_count, 1);
    zassert_equal(device_stubs.downlink.end_fail_count, 1);
    zassert_equal(device_stubs.downlink.end_success_count, 0);
}

ZTEST(serial_device, test_receiver_missing_first_flag)
{
    /* Send a DATA frame without FIRST to a channel that isn't open */
    int err = send_data(POUCH_SERIAL_CH_DOWNLINK, false, false, false, (uint8_t[]){0x01}, 1);
    zassert_not_equal(err, 0, "expected error for missing FIRST flag");

    /* Endpoint should not have been started successfully */
    zassert_equal(device_stubs.downlink.end_success_count, 0);
}

ZTEST(serial_device, test_receiver_both_channels)
{
    static const uint8_t server_cert[] = {0x10, 0x20};
    static const uint8_t downlink[] = {0x30, 0x40, 0x50};

    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT,
                         true,
                         true,
                         false,
                         server_cert,
                         sizeof(server_cert)));
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, true, false, downlink, sizeof(downlink)));

    /* Drain all ACKs */
    uint8_t buf[FRAME_BUF_SIZE];
    for (int i = 0; i < MAX_FRAMES; i++)
    {
        if (pouch_serial_device_frame_get(buf, sizeof(buf)) == 0)
        {
            break;
        }
    }

    zassert_equal(device_stubs.server_cert.rx_len, sizeof(server_cert));
    zassert_mem_equal(device_stubs.server_cert.rx_buf, server_cert, sizeof(server_cert));

    zassert_equal(device_stubs.downlink.rx_len, sizeof(downlink));
    zassert_mem_equal(device_stubs.downlink.rx_buf, downlink, sizeof(downlink));
}

ZTEST(serial_device, test_receiver_start_error)
{
    device_stubs.downlink.start_err = -EBUSY;

    int err = send_data(POUCH_SERIAL_CH_DOWNLINK, true, true, false, (uint8_t[]){0x01}, 1);
    zassert_not_equal(err, 0);

    /* Device should produce a NACK */
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;
    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));

    zassert_true(len > 0, "expected NACK frame");
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_DOWNLINK);
    zassert_false(hdr.is_data, "expected ACK (NACK), got DATA");
    zassert_true(hdr.err, "expected ERR flag on NACK");
}

ZTEST(serial_device, test_receiver_recv_error)
{
    device_stubs.server_cert.recv_err = -ENOMEM;

    int err = send_data(POUCH_SERIAL_CH_SERVER_CERT, true, true, false, (uint8_t[]){0xAA}, 1);
    zassert_not_equal(err, 0);

    zassert_equal(device_stubs.server_cert.start_count, 1);
    zassert_equal(device_stubs.server_cert.end_fail_count, 1);
}

/* ==========================================================================
 * Cross-channel and invalid behavior tests
 * ========================================================================== */

ZTEST(serial_device, test_invalid_channel_id)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr = {
        .is_data = false,
        .channel = POUCH_SERIAL_CHANNEL_COUNT, /* Out of range */
    };
    size_t len = build_frame(buf, sizeof(buf), &hdr, NULL, 0);
    int err = pouch_serial_device_recv(buf, len);
    zassert_equal(err, -EINVAL);
}

ZTEST(serial_device, test_recv_null_frame)
{
    int err = pouch_serial_device_recv(NULL, 1);
    zassert_equal(err, -EINVAL);
}

ZTEST(serial_device, test_recv_zero_length)
{
    uint8_t buf[4] = {0};
    int err = pouch_serial_device_recv(buf, 0);
    zassert_equal(err, -EINVAL);
}

ZTEST(serial_device, test_malformed_ack_header)
{
    /*
     * An ACK frame with reserved bits (FIRST or LAST) set is malformed.
     * Manually encode: bit7=0 (ACK), bit5=1 (FIRST, reserved), channel=0
     */
    uint8_t frame[] = {0x20}; /* 0b00100000 = ACK with FIRST bit set */
    int err = pouch_serial_device_recv(frame, sizeof(frame));
    zassert_equal(err, -EINVAL);
}

ZTEST(serial_device, test_interrupt_ongoing_receive)
{
    static const uint8_t frag[] = {0x01, 0x02};

    /* Start a multi-fragment receive */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, false, false, frag, sizeof(frag)));

    /* Send another FIRST frame: interrupts the ongoing transfer */
    int err = send_data(POUCH_SERIAL_CH_DOWNLINK, true, false, false, frag, sizeof(frag));
    zassert_not_equal(err, 0, "expected error when sending FIRST to already-open channel");

    /* The first transfer should have been aborted */
    zassert_equal(device_stubs.downlink.end_fail_count, 1);
}

ZTEST(serial_device, test_concurrent_sender_and_receiver)
{
    static const uint8_t uplink_data[] = {0xAA, 0xBB};
    static const uint8_t downlink_data[] = {0xCC, 0xDD, 0xEE};

    stub_sender_set_data(&device_stubs.uplink, uplink_data, sizeof(uplink_data));

    /* Start both: prompt uplink sender and push downlink data */
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK,
                         true,
                         true,
                         false,
                         downlink_data,
                         sizeof(downlink_data)));

    /* Drain all frames */
    uint8_t buf[FRAME_BUF_SIZE];
    uint8_t uplink_out[STUB_EP_BUF_SIZE];
    size_t uplink_len = 0;
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    for (int i = 0; i < MAX_FRAMES; i++)
    {
        size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
        if (len == 0)
        {
            break;
        }

        if (hdr.channel == POUCH_SERIAL_CH_UPLINK && hdr.is_data && payload_len > 0)
        {
            memcpy(&uplink_out[uplink_len], payload, payload_len);
            uplink_len += payload_len;
        }
    }

    /* Verify uplink data was sent correctly */
    zassert_equal(uplink_len, sizeof(uplink_data));
    zassert_mem_equal(uplink_out, uplink_data, sizeof(uplink_data));

    /* Verify downlink data was received correctly */
    zassert_equal(device_stubs.downlink.rx_len, sizeof(downlink_data));
    zassert_mem_equal(device_stubs.downlink.rx_buf, downlink_data, sizeof(downlink_data));
}

ZTEST(serial_device, test_ready_callback)
{
    static const uint8_t payload[] = {0x01};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    /* Start a transfer so the stub captures the bearer pointer */
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    /* Drain all frames to complete the transfer */
    uint8_t buf[FRAME_BUF_SIZE];
    for (int i = 0; i < MAX_FRAMES; i++)
    {
        if (pouch_serial_device_frame_get(buf, sizeof(buf)) == 0)
        {
            break;
        }
    }

    /* Set up a new transfer and use bearer->ready() to signal readiness */
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    /* Drain the first frame */
    pouch_serial_device_frame_get(buf, sizeof(buf));

    int count_before = ready_count;

    /* Simulate endpoint signalling readiness via bearer */
    zassert_not_null(device_stubs.uplink.bearer);
    pouch_bearer_ready(device_stubs.uplink.bearer);

    zassert_true(ready_count > count_before,
                 "ready callback not called (before=%d, after=%d)",
                 count_before,
                 ready_count);
}

ZTEST(serial_device, test_frame_get_no_pending)
{
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = pouch_serial_device_frame_get(buf, sizeof(buf));
    zassert_equal(len, 0, "expected no frames from idle device");
}

ZTEST(serial_device, test_sender_ack_err_during_transfer)
{
    /* Payload must span multiple frames so the channel stays open after the first frame.
     * Each frame carries at most FRAME_BUF_SIZE - POUCH_SERIAL_HEADER_LEN payload bytes.
     */
    uint8_t payload[FRAME_BUF_SIZE * 2];
    memset(payload, 0xAB, sizeof(payload));
    stub_sender_set_data(&device_stubs.info, payload, sizeof(payload));

    /* Start the transfer */
    zassert_ok(send_ack(POUCH_SERIAL_CH_INFO, false));

    /* Drain the first data frame (don't drain all: we want to interrupt) */
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    size_t len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected first data frame");
    zassert_true(hdr.first, "expected FIRST flag on first frame");
    zassert_false(hdr.last, "first frame should not be LAST for multi-fragment transfer");

    /* Now send ACK+ERR to abort the ongoing sender transfer */
    zassert_ok(send_ack(POUCH_SERIAL_CH_INFO, true));

    /* Endpoint should have been aborted */
    zassert_equal(device_stubs.info.end_fail_count, 1);
    zassert_equal(device_stubs.info.end_success_count, 0);
}

ZTEST(serial_device, test_successive_transfers_same_channel)
{
    static const uint8_t data1[] = {0x01, 0x02};
    static const uint8_t data2[] = {0x03, 0x04, 0x05};

    /* First transfer */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, true, false, data1, sizeof(data1)));

    /* Drain ACK */
    uint8_t buf[FRAME_BUF_SIZE];
    for (int i = 0; i < MAX_FRAMES; i++)
    {
        if (pouch_serial_device_frame_get(buf, sizeof(buf)) == 0)
        {
            break;
        }
    }

    zassert_equal(device_stubs.downlink.end_success_count, 1);

    /* Second transfer on same channel */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, true, false, data2, sizeof(data2)));

    for (int i = 0; i < MAX_FRAMES; i++)
    {
        if (pouch_serial_device_frame_get(buf, sizeof(buf)) == 0)
        {
            break;
        }
    }

    zassert_equal(device_stubs.downlink.start_count, 2);
    zassert_equal(device_stubs.downlink.end_success_count, 2);
    zassert_equal(device_stubs.downlink.rx_len, sizeof(data2));
    zassert_mem_equal(device_stubs.downlink.rx_buf, data2, sizeof(data2));
}

ZTEST(serial_device, test_successive_sender_transfers)
{
    static const uint8_t data1[] = {0xAA};
    static const uint8_t data2[] = {0xBB, 0xCC};

    /* First uplink transfer */
    stub_sender_set_data(&device_stubs.uplink, data1, sizeof(data1));
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;
    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
                        out,
                        &out_len,
                        sizeof(out),
                        &frame_count,
                        &saw_first,
                        &saw_last,
                        &saw_err);

    zassert_equal(out_len, sizeof(data1));
    zassert_equal(device_stubs.uplink.end_success_count, 1);

    /* Second uplink transfer */
    stub_sender_set_data(&device_stubs.uplink, data2, sizeof(data2));
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
                        out,
                        &out_len,
                        sizeof(out),
                        &frame_count,
                        &saw_first,
                        &saw_last,
                        &saw_err);

    zassert_equal(out_len, sizeof(data2));
    zassert_mem_equal(out, data2, sizeof(data2));
    zassert_equal(device_stubs.uplink.start_count, 2);
    zassert_equal(device_stubs.uplink.end_success_count, 2);
}

ZTEST(serial_device, test_init_with_null_callback)
{
    /* Should not crash with NULL ready callback */
    pouch_serial_device_init(NULL);

    static const uint8_t payload[] = {0x01};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;
    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
                        out,
                        &out_len,
                        sizeof(out),
                        &frame_count,
                        &saw_first,
                        &saw_last,
                        &saw_err);

    zassert_true(saw_last);
    zassert_equal(out_len, sizeof(payload));

    /* Restore for other tests */
    pouch_serial_device_init(ready_cb);
}
