/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Device-side firmware relay.
 *
 * The relay is driven from both ends: the application writes image bytes in
 * through the public API, and the broker drains them out through the serial
 * firmware endpoint. These tests drive the endpoint vtables directly, so no
 * serial core is involved.
 *
 * The case that matters most is the last one: an image the host rejects has to
 * be restartable from the apply verdict, because that verdict is the only
 * notification the device ever gets.
 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

#include <pouch/transport/serial/fw.h>

#include "transport/endpoints/device/endpoints.h"

#define IMG_SIZE 256

static uint8_t image[IMG_SIZE];
static const uint8_t hash[32] = {0xaa, 0xbb, 0xcc};

/* Verdicts seen by the registered callback. */
static enum pouch_serial_fw_status seen[8];
static int seen_count;

static void record_status(enum pouch_serial_fw_status status)
{
    if (seen_count < (int) ARRAY_SIZE(seen))
    {
        seen[seen_count] = status;
    }
    seen_count++;
}

/** Deliver an apply verdict the way the broker's FW_STATUS frame would. */
static int deliver_status(uint8_t status)
{
    return pouch_device_endpoint_fw_status.recv(NULL, &status, 1);
}

/**
 * Drain the firmware endpoint until it reports the transfer finished.
 *
 * Returns the total number of bytes collected, header included, or a negative
 * value if the endpoint kept asking to be called again - which means the relay
 * never finished, the bug that would show up as a stalled update.
 */
static int drain_relay(uint8_t *out, size_t out_size)
{
    size_t total = 0;

    zassert_ok(pouch_device_endpoint_fw.start(NULL));

    for (int i = 0; i < 64; i++)
    {
        size_t len = out_size - total;
        enum pouch_result result = pouch_device_endpoint_fw.send(NULL, &out[total], &len);

        zassert_not_equal(result, POUCH_ERROR, "relay reported an error while draining");
        total += len;

        if (result == POUCH_NO_MORE_DATA)
        {
            return (int) total;
        }
    }

    return -EAGAIN;
}

static void before(void *f)
{
    ARG_UNUSED(f);

    /* Leave no relay in flight for the next test. */
    pouch_serial_fw_abort();
    pouch_serial_fw_status_callback_set(NULL);

    seen_count = 0;
    memset(seen, 0, sizeof(seen));

    for (size_t i = 0; i < sizeof(image); i++)
    {
        image[i] = (uint8_t) i;
    }
}

ZTEST_SUITE(serial_fw, NULL, NULL, before, NULL, NULL);

/* ---- the apply verdict ---------------------------------------------------- */

ZTEST(serial_fw, test_status_reaches_the_callback)
{
    pouch_serial_fw_status_callback_set(record_status);

    zassert_ok(deliver_status(POUCH_SERIAL_FW_STATUS_HASH_FAIL));

    zassert_equal(seen_count, 1, "callback was not invoked");
    zassert_equal(seen[0], POUCH_SERIAL_FW_STATUS_HASH_FAIL);

    enum pouch_serial_fw_status polled;
    zassert_true(pouch_serial_fw_status_get(&polled));
    zassert_equal(polled, POUCH_SERIAL_FW_STATUS_HASH_FAIL);
}

ZTEST(serial_fw, test_unknown_status_is_rejected)
{
    pouch_serial_fw_status_callback_set(record_status);

    zassert_equal(deliver_status(0xff), -EINVAL);
    zassert_equal(seen_count, 0, "callback ran for an unknown status");
}

ZTEST(serial_fw, test_empty_status_frame_ignored)
{
    pouch_serial_fw_status_callback_set(record_status);

    zassert_ok(pouch_device_endpoint_fw_status.recv(NULL, "", 0));
    zassert_equal(seen_count, 0);
}

/* ---- relaying an image ---------------------------------------------------- */

ZTEST(serial_fw, test_relay_carries_header_and_image)
{
    uint8_t out[512];

    zassert_ok(pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash));
    zassert_true(pouch_serial_fw_active());

    zassert_equal(pouch_serial_fw_write(image, sizeof(image), POUCH_NO_WAIT), IMG_SIZE);
    pouch_serial_fw_end();

    int total = drain_relay(out, sizeof(out));
    zassert_true(total > 0, "relay never completed (%d)", total);

    size_t hdr_len = POUCH_SERIAL_FW_HDR_FIXED_LEN + strlen("1.2.3") + strlen("main");
    zassert_equal((size_t) total, hdr_len + IMG_SIZE, "unexpected relay length");

    /* Header: magic, total size, and the offset this chunk starts at. */
    zassert_equal(sys_get_le32(&out[0]), POUCH_SERIAL_FW_MAGIC);
    zassert_equal(sys_get_le32(&out[4]), IMG_SIZE);
    zassert_equal(sys_get_le32(&out[8]), 0, "first chunk should start at offset 0");
    zassert_mem_equal(&out[12], hash, sizeof(hash));

    zassert_mem_equal(&out[hdr_len], image, IMG_SIZE, "image bytes were not relayed intact");
    zassert_false(pouch_serial_fw_active(), "relay still active after the image was drained");
}

ZTEST(serial_fw, test_begin_rejects_a_second_relay)
{
    zassert_ok(pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash));
    zassert_equal(pouch_serial_fw_begin("main", "1.2.4", IMG_SIZE, hash), -EBUSY);
}

ZTEST(serial_fw, test_abort_releases_the_relay)
{
    zassert_ok(pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash));
    pouch_serial_fw_abort();

    zassert_false(pouch_serial_fw_active());
    zassert_ok(pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash));
}

/* ---- the retry path ------------------------------------------------------- */

/*
 * Restarting a rejected image from inside the verdict callback.
 *
 * This is the whole recovery path for a rejected update: the device has already
 * told the cloud to stop offering the component, and the OTA manifest that
 * would re-offer it arrives only once per connection, so if the relay cannot be
 * restarted from here the update never retries. The callback runs on the serial
 * receive path, so it has to be able to re-enter this module.
 */
static int retry_err;
static int retry_count;

static void retry_on_reject(enum pouch_serial_fw_status status)
{
    record_status(status);

    if (status == POUCH_SERIAL_FW_STATUS_OK)
    {
        return;
    }

    retry_count++;
    pouch_serial_fw_abort();
    retry_err = pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash);
}

ZTEST(serial_fw, test_rejected_image_can_be_restarted_from_the_callback)
{
    uint8_t out[512];

    pouch_serial_fw_status_callback_set(retry_on_reject);

    zassert_ok(pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash));
    zassert_equal(pouch_serial_fw_write(image, sizeof(image), POUCH_NO_WAIT), IMG_SIZE);
    pouch_serial_fw_end();
    zassert_true(drain_relay(out, sizeof(out)) > 0);

    /* The host verifies the image and reports back that it did not take it. */
    zassert_ok(deliver_status(POUCH_SERIAL_FW_STATUS_HASH_FAIL));

    zassert_equal(retry_count, 1, "the verdict did not trigger a retry");
    zassert_ok(retry_err, "restarting the relay from the callback failed (%d)", retry_err);
    zassert_true(pouch_serial_fw_active(), "the retry left no relay armed");

    /* The retry relays the whole image again, from the start. */
    zassert_equal(pouch_serial_fw_write(image, sizeof(image), POUCH_NO_WAIT), IMG_SIZE);
    pouch_serial_fw_end();

    int total = drain_relay(out, sizeof(out));
    size_t hdr_len = POUCH_SERIAL_FW_HDR_FIXED_LEN + strlen("1.2.3") + strlen("main");

    zassert_equal((size_t) total, hdr_len + IMG_SIZE, "the retry did not resend the whole image");
    zassert_equal(sys_get_le32(&out[8]), 0, "the retry should restart at offset 0");
    zassert_mem_equal(&out[hdr_len], image, IMG_SIZE);
}

ZTEST(serial_fw, test_accepted_image_is_not_restarted)
{
    uint8_t out[512];

    pouch_serial_fw_status_callback_set(retry_on_reject);
    retry_count = 0;

    zassert_ok(pouch_serial_fw_begin("main", "1.2.3", IMG_SIZE, hash));
    zassert_equal(pouch_serial_fw_write(image, sizeof(image), POUCH_NO_WAIT), IMG_SIZE);
    pouch_serial_fw_end();
    zassert_true(drain_relay(out, sizeof(out)) > 0);

    zassert_ok(deliver_status(POUCH_SERIAL_FW_STATUS_OK));

    zassert_equal(retry_count, 0, "an accepted image was retried");
    zassert_false(pouch_serial_fw_active());
}
