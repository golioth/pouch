/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Device-side signed-URL handoff.
 *
 * The application announces an artifact through the public API and the broker
 * collects a record describing it through the serial endpoint. These tests
 * drive the endpoint vtables directly, so no serial core is involved.
 *
 * Two properties matter more than the rest. A signature that cannot be produced
 * yet - no signer, or a clock that has not been set - must leave the handoff
 * pending rather than failing the transfer, because the session it would fail
 * is the one carrying everything else. And a rejected artifact has to be
 * re-announceable from the apply verdict, which is the only notification the
 * device ever gets.
 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <pouch/transport/serial/fw_url.h>

#include "transport/endpoints/device/endpoints.h"

#define ARTIFACT_SIZE 250948
#define BASE_URL "https://gw.golioth.io/.u/c/rpmsg_device@1.0.3"

/* What the fake signer appends, standing in for nb/na/cert/sig. */
#define SIGNATURE_SUFFIX "?nb=1000&na=1090&cert=Y2VydA&sig=c2ln"

static const uint8_t hash[32] = {0xaa, 0xbb, 0xcc, 0xdd};

static int sign_err;             /* what the fake signer returns */
static size_t sign_len_override; /* non-zero: claim this length instead */
static int sign_calls;

static int fake_sign(const char *url, size_t url_len, char *out, size_t out_len, size_t *olen)
{
    sign_calls++;

    if (sign_err != 0)
    {
        return sign_err;
    }

    if (sign_len_override != 0)
    {
        *olen = sign_len_override;
        return 0;
    }

    int n = snprintf(out, out_len, "%.*s%s", (int) url_len, url, SIGNATURE_SUFFIX);
    if (n < 0 || (size_t) n >= out_len)
    {
        return -ENOMEM;
    }

    *olen = (size_t) n;
    return 0;
}

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
 * Collect the URL endpoint until it reports the transfer finished.
 *
 * Returns the number of bytes collected, or a negative value if the endpoint
 * kept asking to be called again - which would stall the broker's session.
 */
static int collect_url(uint8_t *out, size_t out_size)
{
    size_t total = 0;

    zassert_ok(pouch_device_endpoint_fw_url.start(NULL));

    for (int i = 0; i < 64; i++)
    {
        size_t len = out_size - total;
        enum pouch_result result = pouch_device_endpoint_fw_url.send(NULL, &out[total], &len);

        zassert_not_equal(result, POUCH_ERROR, "handoff reported an error while collecting");
        total += len;

        if (result == POUCH_NO_MORE_DATA)
        {
            return (int) total;
        }
    }

    return -EAGAIN;
}

/** Push a time record through the endpoint the way a broker frame would. */
static void deliver_time(const uint8_t *rec, size_t len, bool success)
{
    zassert_ok(pouch_device_endpoint_time.start(NULL));
    if (len > 0)
    {
        zassert_ok(pouch_device_endpoint_time.recv(NULL, rec, len));
    }
    pouch_device_endpoint_time.end(NULL, success);
}

static void build_time_record(uint8_t rec[POUCH_SERIAL_TIME_LEN], uint32_t magic, int64_t seconds)
{
    sys_put_le32(magic, &rec[0]);
    sys_put_le64((uint64_t) seconds, &rec[4]);
}

/* Seconds reported to the time callback. */
static int64_t time_seen;
static int time_calls;

static void record_time(int64_t seconds)
{
    time_seen = seconds;
    time_calls++;
}

static void before(void *f)
{
    ARG_UNUSED(f);

    /* Leave no handoff pending for the next test. */
    pouch_serial_fw_url_abort();
    pouch_serial_fw_url_signer_set(fake_sign);
    pouch_serial_fw_status_callback_set(NULL);
    pouch_serial_time_callback_set(NULL);

    sign_err = 0;
    sign_len_override = 0;
    sign_calls = 0;
    seen_count = 0;
    time_calls = 0;
    time_seen = 0;
    memset(seen, 0, sizeof(seen));
}

ZTEST_SUITE(pouch_serial_fw_url, NULL, NULL, before, NULL, NULL);

ZTEST(pouch_serial_fw_url, test_idle_poll_is_an_empty_transfer)
{
    uint8_t buf[64];

    zassert_false(pouch_serial_fw_url_active());
    zassert_equal(collect_url(buf, sizeof(buf)), 0, "an idle handoff should hand over nothing");
    zassert_equal(sign_calls, 0, "an idle handoff should not sign anything");
}

ZTEST(pouch_serial_fw_url, test_announced_artifact_is_handed_over)
{
    uint8_t buf[512];

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_true(pouch_serial_fw_url_active());

    int len = collect_url(buf, sizeof(buf));

    const char *version = "1.0.3";
    const char *package = "rpmsg_device";
    size_t ver_len = strlen(version);
    size_t pkg_len = strlen(package);
    const char *expect_url = BASE_URL SIGNATURE_SUFFIX;
    size_t url_len = strlen(expect_url);

    zassert_equal(len,
                  (int) (POUCH_SERIAL_FW_URL_HDR_FIXED_LEN + ver_len + pkg_len + url_len),
                  "record length should cover the header, both names and the signed URL");

    zassert_equal(sys_get_le32(&buf[0]), POUCH_SERIAL_FW_URL_MAGIC);
    zassert_equal(sys_get_le32(&buf[4]), ARTIFACT_SIZE, "artifact size should be on the wire");
    zassert_mem_equal(&buf[8], hash, sizeof(hash), "the manifest digest should be on the wire");
    zassert_equal(buf[40], ver_len);
    zassert_equal(buf[41], pkg_len);
    zassert_equal(sys_get_le16(&buf[42]), url_len);

    size_t off = POUCH_SERIAL_FW_URL_HDR_FIXED_LEN;
    zassert_mem_equal(&buf[off], version, ver_len, "version should follow the header");
    off += ver_len;
    zassert_mem_equal(&buf[off], package, pkg_len, "package should follow the version");
    off += pkg_len;
    zassert_mem_equal(&buf[off], expect_url, url_len, "the signed URL should follow the names");

    zassert_equal(sign_calls, 1, "the URL should be signed exactly once per handoff");

    /* Handed over: the host owns the fetch now, and a second poll finds nothing. */
    zassert_false(pouch_serial_fw_url_active());
    zassert_equal(collect_url(buf, sizeof(buf)), 0);
}

ZTEST(pouch_serial_fw_url, test_record_survives_a_small_collection_buffer)
{
    uint8_t whole[512];
    uint8_t piecemeal[512];

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    int len = collect_url(whole, sizeof(whole));
    zassert_true(len > 64, "record should be larger than one collection below");

    /* The broker's frames are smaller than the record, so the endpoint has to
     * be called repeatedly and must produce the same bytes either way.
     */
    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_ok(pouch_device_endpoint_fw_url.start(NULL));

    size_t total = 0;
    for (int i = 0; i < 64; i++)
    {
        size_t chunk = 16;
        enum pouch_result result =
            pouch_device_endpoint_fw_url.send(NULL, &piecemeal[total], &chunk);
        zassert_not_equal(result, POUCH_ERROR);
        total += chunk;
        if (result == POUCH_NO_MORE_DATA)
        {
            break;
        }
    }

    zassert_equal((int) total, len, "a fragmented collection should carry the same record");
    zassert_mem_equal(piecemeal, whole, total);
}

ZTEST(pouch_serial_fw_url, test_unsignable_handoff_stays_pending)
{
    uint8_t buf[512];

    sign_err = -EINVAL;

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));

    /* A signer that cannot sign yet - typically a clock that has not been set -
     * must not fail the transfer: the session carries the rest of Pouch too.
     */
    zassert_equal(collect_url(buf, sizeof(buf)), 0, "a failed signature should hand over nothing");
    zassert_true(pouch_serial_fw_url_active(), "the handoff should still be pending");

    /* Once signing works, the next poll picks it up. */
    sign_err = 0;
    int len = collect_url(buf, sizeof(buf));
    zassert_true(len > 0, "the handoff should be signed and collected on a later poll");
    zassert_false(pouch_serial_fw_url_active());
}

ZTEST(pouch_serial_fw_url, test_handoff_without_a_signer_stays_pending)
{
    uint8_t buf[512];

    pouch_serial_fw_url_signer_set(NULL);

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_equal(collect_url(buf, sizeof(buf)), 0);
    zassert_true(pouch_serial_fw_url_active());
}

ZTEST(pouch_serial_fw_url, test_oversized_signature_is_refused)
{
    uint8_t buf[512];

    /* A signer that reports more than the record can hold must not be trusted
     * about the length: the bytes it claims are not there.
     */
    sign_len_override = CONFIG_POUCH_SERIAL_FW_URL_MAX_LEN + 1;

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_equal(collect_url(buf, sizeof(buf)), 0);
    zassert_true(pouch_serial_fw_url_active());
}

ZTEST(pouch_serial_fw_url, test_begin_rejects_bad_input)
{
    char long_name[POUCH_SERIAL_FW_MAX_NAME_LEN + 2];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    zassert_equal(pouch_serial_fw_url_begin(NULL, "1.0.3", ARTIFACT_SIZE, hash, BASE_URL), -EINVAL);
    zassert_equal(pouch_serial_fw_url_begin("pkg", NULL, ARTIFACT_SIZE, hash, BASE_URL), -EINVAL);
    zassert_equal(pouch_serial_fw_url_begin("pkg", "1.0.3", ARTIFACT_SIZE, NULL, BASE_URL),
                  -EINVAL);
    zassert_equal(pouch_serial_fw_url_begin("pkg", "1.0.3", ARTIFACT_SIZE, hash, NULL), -EINVAL);
    zassert_equal(pouch_serial_fw_url_begin("pkg", "1.0.3", 0, hash, BASE_URL), -EINVAL);
    zassert_equal(pouch_serial_fw_url_begin("", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL), -EINVAL);
    zassert_equal(pouch_serial_fw_url_begin(long_name, "1.0.3", ARTIFACT_SIZE, hash, BASE_URL),
                  -EINVAL,
                  "a name longer than the wire field should be refused");
    zassert_equal(pouch_serial_fw_url_begin("pkg", long_name, ARTIFACT_SIZE, hash, BASE_URL),
                  -EINVAL);

    zassert_false(pouch_serial_fw_url_active(), "no bad input should leave a handoff pending");
}

ZTEST(pouch_serial_fw_url, test_second_announcement_is_refused)
{
    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_equal(pouch_serial_fw_url_begin("rpmsg_device", "1.0.4", ARTIFACT_SIZE, hash, BASE_URL),
                  -EBUSY);
}

ZTEST(pouch_serial_fw_url, test_abort_releases_the_handoff)
{
    uint8_t buf[512];

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    pouch_serial_fw_url_abort();

    zassert_false(pouch_serial_fw_url_active());
    zassert_equal(collect_url(buf, sizeof(buf)), 0);

    /* And a later offer starts cleanly. */
    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.4", ARTIFACT_SIZE, hash, BASE_URL));
}

ZTEST(pouch_serial_fw_url, test_time_record_is_accepted)
{
    uint8_t rec[POUCH_SERIAL_TIME_LEN];

    pouch_serial_time_callback_set(record_time);
    build_time_record(rec, POUCH_SERIAL_TIME_MAGIC, 1757000000);

    deliver_time(rec, sizeof(rec), true);

    zassert_equal(time_calls, 1, "the handler should see the broker's clock");
    zassert_equal(time_seen, 1757000000);
    zassert_true(pouch_serial_time_synced(), "a device told the time knows it has a host");
}

ZTEST(pouch_serial_fw_url, test_malformed_time_records_are_ignored)
{
    uint8_t rec[POUCH_SERIAL_TIME_LEN];

    pouch_serial_time_callback_set(record_time);

    /* Wrong magic. */
    build_time_record(rec, 0xdeadbeef, 1757000000);
    deliver_time(rec, sizeof(rec), true);

    /* Too short. */
    build_time_record(rec, POUCH_SERIAL_TIME_MAGIC, 1757000000);
    deliver_time(rec, sizeof(rec) - 1, true);

    /* A transfer the broker abandoned. */
    deliver_time(rec, sizeof(rec), false);

    /* An empty transfer, which is what a broker with no clock would send. */
    deliver_time(NULL, 0, true);

    zassert_equal(time_calls, 0, "nothing malformed should reach the handler");
}

ZTEST(pouch_serial_fw_url, test_rejected_artifact_can_be_reannounced_from_the_callback)
{
    uint8_t buf[512];

    pouch_serial_fw_status_callback_set(record_status);

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_true(collect_url(buf, sizeof(buf)) > 0);
    zassert_false(pouch_serial_fw_url_active(), "handed over, so no longer pending");

    /* The verdict is the only notification the device gets: the cloud was told
     * to stop offering the component when this was announced. Re-announcing
     * from inside the handler is what has to work.
     */
    zassert_ok(deliver_status(POUCH_SERIAL_FW_STATUS_HASH_FAIL));
    zassert_equal(seen_count, 1);
    zassert_equal(seen[0], POUCH_SERIAL_FW_STATUS_HASH_FAIL);

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));
    zassert_true(collect_url(buf, sizeof(buf)) > 0, "the retry should be collectable");
}

ZTEST(pouch_serial_fw_url, test_announcing_clears_a_stale_verdict)
{
    enum pouch_serial_fw_status status;

    zassert_ok(deliver_status(POUCH_SERIAL_FW_STATUS_OK));
    zassert_true(pouch_serial_fw_status_get(&status));

    zassert_ok(pouch_serial_fw_url_begin("rpmsg_device", "1.0.3", ARTIFACT_SIZE, hash, BASE_URL));

    zassert_false(pouch_serial_fw_status_get(&status),
                  "a new artifact should not inherit the previous one's verdict");
}
