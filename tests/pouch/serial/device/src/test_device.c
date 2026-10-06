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
#include "transport/endpoints/device/endpoints.h"
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

/** Send an ACK frame to a channel, or a NACK if @p err is set. */
static int send_ack(uint8_t channel, bool err)
{
    struct pouch_serial_header hdr = {
        .is_data = false,
        .err = err,
        .channel = channel,
    };
    return send_frame(&hdr, NULL, 0);
}

/** Abort the transfer on a channel. */
static int send_abort(uint8_t channel)
{
    struct pouch_serial_header hdr = {
        .is_data = false,
        .err = true,
        .last = true,
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
 * Drain a transfer from the device into a contiguous buffer, requesting each
 * fragment after the first with an ACK like the broker does.
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

        /* ACK the fragment, which requests the next one, or completes the transfer */
        zassert_ok(send_ack(channel, false));

        if (hdr.last)
        {
            *saw_last = true;
            break;
        }
    }
}

/**
 * Expect an ACK frame (with the given error flag) on the channel. The device never sends NACKs,
 * so an ACK with the error flag is always an abort.
 */
static void expect_ack(uint8_t channel, bool err)
{
    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *payload;
    size_t payload_len;

    size_t len = get_frame(&hdr, &payload, &payload_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected ACK on channel %u", channel);
    zassert_equal(hdr.channel, channel);
    zassert_false(hdr.is_data, "expected ACK, got DATA");
    zassert_equal(hdr.err, err);
    zassert_equal(hdr.last, err, "expected an abort");
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
    pouch_serial_device_sync();  // unsuspend device
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

ZTEST(serial_device, test_sender_multi_fragment_waits_for_ack)
{
    /* Payload must span multiple frames. Each frame carries at most
     * FRAME_BUF_SIZE - POUCH_SERIAL_HEADER_LEN payload bytes.
     */
    uint8_t payload[FRAME_BUF_SIZE * 2];
    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t) i;
    }
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;

    size_t len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0);
    zassert_true(hdr.first);
    zassert_false(hdr.last, "expected multi-fragment transfer");

    /* The next fragment is only sent once the broker requests it */
    zassert_equal(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)), 0);

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));
    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
                        out,
                        &out_len,
                        sizeof(out),
                        &frame_count,
                        &saw_first,
                        &saw_last,
                        &saw_err);

    zassert_false(saw_first, "FIRST flag on a later fragment");
    zassert_true(saw_last);
    zassert_false(saw_err);
    zassert_equal(resp_len + out_len, sizeof(payload));
    zassert_mem_equal(out, &payload[resp_len], out_len);

    /* The transfer is complete once the last fragment is ACKed */
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

ZTEST(serial_device, test_sender_abort_on_closed_channel)
{
    static const uint8_t payload[] = {0xAA, 0xBB};
    stub_sender_set_data(&device_stubs.info, payload, sizeof(payload));

    /* An abort doesn't start a transfer */
    int err = send_abort(POUCH_SERIAL_CH_INFO);
    zassert_ok(err);

    /* Device should not produce any data frames */
    uint8_t buf[FRAME_BUF_SIZE];
    size_t len = pouch_serial_device_frame_get(buf, sizeof(buf));
    zassert_equal(len, 0, "expected no frames after an abort on a closed channel");

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
    zassert_equal(err, -ENOMEM);

    /* Device should also produce an error DATA frame */
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
    zassert_equal(device_stubs.uplink.end_fail_count, 0);  // never started
}

/*
 * An endpoint may report POUCH_MORE_DATA without producing any bytes when its
 * data isn't ready yet - the real uplink does this on every session, because
 * pouch encrypts on a work queue after the channel has already been prompted.
 * The channel must stay armed across those calls, or the transfer stalls
 * forever.
 */
ZTEST(serial_device, test_sender_data_not_ready_yet)
{
    static const uint8_t payload[] = {0x10, 0x20, 0x30};

    device_stubs.uplink.defer_count = 2;
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;

    /* No data yet: no frame is emitted, and no empty frame either. */
    for (int i = 0; i < 2; i++)
    {
        size_t len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
        zassert_equal(len, 0, "expected no frame while endpoint has no data (call %d)", i);
    }

    /* The channel is still armed, so the data goes out once it is available. */
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

    zassert_true(saw_first, "transfer stalled: no data after the endpoint became ready");
    zassert_true(saw_last, "missing LAST flag");
    zassert_false(saw_err, "unexpected ERR flag");
    zassert_equal(out_len, sizeof(payload));
    zassert_mem_equal(out, payload, sizeof(payload));
    zassert_equal(device_stubs.uplink.end_success_count, 1);
}

/*
 * INFO and DEVICE_CERT have no end() callback, matching the real endpoints.
 * Closing such a channel must not dereference it.
 */
ZTEST(serial_device, test_sender_without_end_callback)
{
    static const uint8_t payload[] = {0xAA, 0xBB};

    zassert_is_null(pouch_device_endpoint_info.end, "INFO stub must mirror the real endpoint");
    zassert_is_null(pouch_device_endpoint_device_cert.end,
                    "DEVICE_CERT stub must mirror the real endpoint");

    stub_sender_set_data(&device_stubs.info, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_INFO, false));

    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;
    drain_sender_frames(POUCH_SERIAL_CH_INFO,
                        out,
                        &out_len,
                        sizeof(out),
                        &frame_count,
                        &saw_first,
                        &saw_last,
                        &saw_err);

    /* The transfer completes and closes the channel without a NULL deref. */
    zassert_true(saw_first);
    zassert_true(saw_last);
    zassert_false(saw_err);
    zassert_equal(out_len, sizeof(payload));
    zassert_mem_equal(out, payload, sizeof(payload));
    zassert_equal(device_stubs.info.start_count, 1);
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

    /* Each fragment is ACKed once it has been processed, including the last one */
    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT, true, false, false, frag1, sizeof(frag1)));
    expect_ack(POUCH_SERIAL_CH_SERVER_CERT, false);

    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT, false, false, false, frag2, sizeof(frag2)));
    expect_ack(POUCH_SERIAL_CH_SERVER_CERT, false);

    zassert_ok(send_data(POUCH_SERIAL_CH_SERVER_CERT, false, true, false, frag3, sizeof(frag3)));
    expect_ack(POUCH_SERIAL_CH_SERVER_CERT, false);

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

    /* Pushed transfers are ACKed, even when the only fragment is also the last one. */
    expect_ack(POUCH_SERIAL_CH_DOWNLINK, false);
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
    zassert_equal(device_stubs.downlink.start_count, 0);
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
    zassert_equal(err, -EBADMSG);
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
     * An ACK frame with the reserved FIRST bit set is malformed.
     * Manually encode: bit7=0 (ACK), bit5=1 (FIRST, reserved), channel=0
     */
    uint8_t frame[] = {0x20}; /* 0b00100000 = ACK with FIRST bit set */
    int err = pouch_serial_device_recv(frame, sizeof(frame));
    zassert_equal(err, -EBADMSG);
}

ZTEST(serial_device, test_malformed_ack_last_without_err)
{
    /* LAST is only valid on an ACK together with ERR, to abort the transfer */
    uint8_t frame[] = {0x10}; /* 0b00010000 = ACK with LAST bit set */
    int err = pouch_serial_device_recv(frame, sizeof(frame));
    zassert_equal(err, -EBADMSG);
}

ZTEST(serial_device, test_ack_with_payload)
{
    /* ACKs don't carry a payload, so this is a broken frame */
    static const uint8_t payload[] = {0x00, 0x00};
    struct pouch_serial_header hdr = {
        .is_data = false,
        .channel = POUCH_SERIAL_CH_UPLINK,
    };
    int err = send_frame(&hdr, payload, sizeof(payload));
    zassert_equal(err, -EBADMSG);
    zassert_equal(device_stubs.uplink.start_count, 0);
}

ZTEST(serial_device, test_interrupt_ongoing_receive)
{
    static const uint8_t frag[] = {0x01, 0x02};

    /* Start a multi-fragment receive */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, false, false, frag, sizeof(frag)));

    /* Send another FIRST frame: interrupts the ongoing transfer */
    int err = send_data(POUCH_SERIAL_CH_DOWNLINK, true, false, false, frag, sizeof(frag));
    zassert_not_equal(err, 0, "expected error when sending FIRST to already-open channel");

    /* The first transfer should not have been aborted, but the second transfer should have been
     * ignored */
    zassert_equal(device_stubs.downlink.end_fail_count, 0);
    zassert_equal(device_stubs.downlink.start_count, 1);
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

ZTEST(serial_device, test_sender_abort_during_transfer)
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

    /* Now abort the ongoing sender transfer */
    zassert_ok(send_abort(POUCH_SERIAL_CH_INFO));

    /*
     * The INFO endpoint has no end() callback (like the real one), so the abort
     * is observed on the wire instead: the broker abandoned the transfer, so the
     * channel closes without responding, and the rest of the payload is never sent.
     */
    len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_equal(len, 0, "closed channel should not produce more frames");

    /* The next prompt starts a new transfer from the top */
    zassert_ok(send_ack(POUCH_SERIAL_CH_INFO, false));
    len = get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf));
    zassert_true(len > 0, "expected a new transfer");
    zassert_true(hdr.first, "expected FIRST flag on the new transfer");
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
    pouch_serial_device_sync();  // unsuspend device

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
    pouch_serial_device_sync();

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

/* ==========================================================================
 * Retries
 * ========================================================================== */

ZTEST(serial_device, test_sender_last_fragment_waits_for_ack)
{
    static const uint8_t payload[] = {0x10, 0x20};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    zassert_true(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)) > 0);
    zassert_true(hdr.last);

    /* The broker might not have received the last fragment, so it's kept until it's ACKed */
    zassert_equal(device_stubs.uplink.end_success_count, 0);

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));
    zassert_equal(device_stubs.uplink.end_success_count, 1);
    zassert_equal(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)), 0);
}

ZTEST(serial_device, test_sender_nack_resends_fragment)
{
    uint8_t payload[FRAME_BUF_SIZE * 2];
    for (size_t i = 0; i < sizeof(payload); i++)
    {
        payload[i] = (uint8_t) i;
    }
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t first[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    size_t first_len = get_frame(&hdr, &resp_payload, &resp_len, first, sizeof(first));
    zassert_true(first_len > 0);
    zassert_true(hdr.first);
    zassert_false(hdr.last, "expected multi-fragment transfer");

    /* The broker didn't get the fragment: the same frame is sent again */
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, true));

    uint8_t again[FRAME_BUF_SIZE];
    size_t again_len = get_frame(&hdr, &resp_payload, &resp_len, again, sizeof(again));
    zassert_equal(again_len, first_len);
    zassert_mem_equal(again, first, first_len);

    /* Only once */
    zassert_equal(get_frame(&hdr, &resp_payload, &resp_len, again, sizeof(again)), 0);

    /* The rest of the transfer continues where it left off */
    uint8_t out[STUB_EP_BUF_SIZE];
    size_t out_len;
    int frame_count;
    bool saw_first, saw_last, saw_err;
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));
    drain_sender_frames(POUCH_SERIAL_CH_UPLINK,
                        out,
                        &out_len,
                        sizeof(out),
                        &frame_count,
                        &saw_first,
                        &saw_last,
                        &saw_err);

    zassert_false(saw_first);
    zassert_true(saw_last);
    zassert_equal(first_len - POUCH_SERIAL_HEADER_LEN + out_len, sizeof(payload));
    zassert_mem_equal(out, &payload[first_len - POUCH_SERIAL_HEADER_LEN], out_len);
    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_device, test_sender_nack_resends_last_fragment)
{
    static const uint8_t payload[] = {0x10, 0x20, 0x30};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t first[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    size_t first_len = get_frame(&hdr, &resp_payload, &resp_len, first, sizeof(first));
    zassert_true(hdr.first);
    zassert_true(hdr.last);

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, true));

    uint8_t again[FRAME_BUF_SIZE];
    size_t again_len = get_frame(&hdr, &resp_payload, &resp_len, again, sizeof(again));
    zassert_equal(again_len, first_len);
    zassert_mem_equal(again, first, first_len);

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));
    zassert_equal(device_stubs.uplink.start_count, 1);
    zassert_equal(device_stubs.uplink.end_success_count, 1);
}

ZTEST(serial_device, test_sender_nack_before_first_fragment)
{
    static const uint8_t payload[] = {0x10};

    device_stubs.uplink.defer_count = 1;
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    zassert_equal(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)), 0);

    /* Nothing has been sent yet, so there's nothing to send again */
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, true));

    zassert_true(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)) > 0);
    zassert_true(hdr.first);
    zassert_true(hdr.last);
    zassert_equal(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)), 0);
    zassert_equal(device_stubs.uplink.start_count, 1);
}

ZTEST(serial_device, test_sender_nack_opens_closed_channel)
{
    static const uint8_t payload[] = {0x10};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    /* A NACK on a closed channel is a request, just like an ACK */
    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, true));

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    zassert_true(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)) > 0);
    zassert_true(hdr.first);
    zassert_equal(device_stubs.uplink.start_count, 1);
}

ZTEST(serial_device, test_sender_request_while_suspended)
{
    static const uint8_t payload[] = {0x10, 0x20};
    stub_sender_set_data(&device_stubs.uplink, payload, sizeof(payload));

    /* Uplink stays suspended until the application syncs */
    pouch_serial_device_init(ready_cb);

    zassert_ok(send_ack(POUCH_SERIAL_CH_UPLINK, false));

    uint8_t buf[FRAME_BUF_SIZE];
    struct pouch_serial_header hdr;
    const uint8_t *resp_payload;
    size_t resp_len;
    zassert_equal(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)), 0);
    zassert_equal(device_stubs.uplink.start_count, 0);

    /* The broker only requests the transfer once, so the device holds on to the request */
    pouch_serial_device_sync();

    zassert_true(get_frame(&hdr, &resp_payload, &resp_len, buf, sizeof(buf)) > 0);
    zassert_equal(hdr.channel, POUCH_SERIAL_CH_UPLINK);
    zassert_true(hdr.first);
    zassert_equal(resp_len, sizeof(payload));
    zassert_mem_equal(resp_payload, payload, sizeof(payload));
}

ZTEST(serial_device, test_receiver_duplicate_last_fragment)
{
    static const uint8_t data1[] = {0x01, 0x02};
    static const uint8_t data2[] = {0x03};

    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, true, false, false, data1, sizeof(data1)));
    expect_ack(POUCH_SERIAL_CH_DOWNLINK, false);
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, false, true, false, data2, sizeof(data2)));
    expect_ack(POUCH_SERIAL_CH_DOWNLINK, false);
    zassert_equal(device_stubs.downlink.end_success_count, 1);

    /* The ACK was lost, so the broker sends the last fragment again: ACK it without delivering */
    zassert_ok(send_data(POUCH_SERIAL_CH_DOWNLINK, false, true, false, data2, sizeof(data2)));
    expect_ack(POUCH_SERIAL_CH_DOWNLINK, false);

    zassert_equal(device_stubs.downlink.rx_len, sizeof(data1) + sizeof(data2));
    zassert_equal(device_stubs.downlink.start_count, 1);
    zassert_equal(device_stubs.downlink.end_success_count, 1);
    zassert_equal(device_stubs.downlink.end_fail_count, 0);
}
