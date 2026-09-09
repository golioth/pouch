/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <pouch/port.h>
#include <pouch/transport/serial/fw_url.h>
#include <pouch/types.h>

#include "endpoints.h"
#include "fw_internal.h"

POUCH_LOG_REGISTER(pouch_fw_url, CONFIG_POUCH_COMMON_LOG_LEVEL);

/*
 * Device-side signed-URL handoff.
 *
 * The application announces an artifact; this module hands the host a record
 * describing it, with a freshly signed URL the host can fetch it from. The
 * record is small and fixed, so unlike the relay there is no ring buffer and no
 * backpressure - the whole thing is built once and streamed out.
 */

#ifndef CONFIG_POUCH_SERIAL_FW_URL_MAX_LEN
#define CONFIG_POUCH_SERIAL_FW_URL_MAX_LEN 1024
#endif

/*
 * The unsigned URL is a base plus "package@version". Signing expands it by the
 * timestamps, the encoded certificate and the signature, which is why the
 * signed buffer is sized separately and much larger.
 */
#define UNSIGNED_URL_MAX 192

#define REC_MAX                                                           \
    (POUCH_SERIAL_FW_URL_HDR_FIXED_LEN + 2 * POUCH_SERIAL_FW_MAX_NAME_LEN \
     + CONFIG_POUCH_SERIAL_FW_URL_MAX_LEN)

struct fw_url
{
    pouch_mutex_t lock;

    char url[UNSIGNED_URL_MAX]; /* unsigned, kept for re-signing */
    uint8_t rec[REC_MAX];
    size_t names_len; /* fixed header + version + package, set by begin() */
    size_t rec_len;   /* whole record, set once signed */
    size_t rec_sent;

    bool active;    /* announced, not yet handed over */
    bool signed_ok; /* rec holds a signed record for this transfer */
};

static struct fw_url handoff;
static bool inited;
static pouch_serial_fw_url_sign_t signer;

static struct
{
    pouch_serial_time_cb_t cb;
    uint8_t buf[POUCH_SERIAL_TIME_LEN];
    size_t len;
    bool synced;
} wallclock;

static void init_once(void)
{
    if (!inited)
    {
        pouch_mutex_init(&handoff.lock);
        inited = true;
    }
}

static uint32_t get_le32(const uint8_t *src)
{
    return (uint32_t) src[0] | ((uint32_t) src[1] << 8) | ((uint32_t) src[2] << 16)
        | ((uint32_t) src[3] << 24);
}

void pouch_serial_fw_url_signer_set(pouch_serial_fw_url_sign_t sign)
{
    signer = sign;
}

int pouch_serial_fw_url_begin(const char *package,
                              const char *version,
                              uint32_t size,
                              const uint8_t sha256[32],
                              const char *url)
{
    if (!package || !version || !sha256 || !url || size == 0)
    {
        return -EINVAL;
    }

    size_t pkg_len = strlen(package);
    size_t ver_len = strlen(version);
    size_t url_len = strlen(url);

    if (pkg_len == 0 || ver_len == 0 || url_len == 0)
    {
        return -EINVAL;
    }

    if (pkg_len > POUCH_SERIAL_FW_MAX_NAME_LEN || ver_len > POUCH_SERIAL_FW_MAX_NAME_LEN
        || url_len >= sizeof(handoff.url))
    {
        return -EINVAL;
    }

    init_once();
    pouch_mutex_lock(&handoff.lock, POUCH_FOREVER);

    if (handoff.active)
    {
        pouch_mutex_unlock(&handoff.lock);
        return -EBUSY;
    }

    /* The URL length is only known once signed, so it is filled in then. */
    uint8_t *h = handoff.rec;
    pouch_serial_put_le32(&h[0], POUCH_SERIAL_FW_URL_MAGIC);
    pouch_serial_put_le32(&h[4], size);
    memcpy(&h[8], sha256, 32);
    h[40] = (uint8_t) ver_len;
    h[41] = (uint8_t) pkg_len;
    pouch_serial_put_le16(&h[42], 0);
    memcpy(&h[POUCH_SERIAL_FW_URL_HDR_FIXED_LEN], version, ver_len);
    memcpy(&h[POUCH_SERIAL_FW_URL_HDR_FIXED_LEN + ver_len], package, pkg_len);

    handoff.names_len = POUCH_SERIAL_FW_URL_HDR_FIXED_LEN + ver_len + pkg_len;
    memcpy(handoff.url, url, url_len + 1);

    handoff.rec_len = 0;
    handoff.rec_sent = 0;
    handoff.signed_ok = false;
    handoff.active = true;

    pouch_mutex_unlock(&handoff.lock);

    /* Any verdict still held belongs to the previous image. */
    pouch_serial_fw_status_reset();

    POUCH_LOG_INF("Firmware handoff announced: %s %s, %u bytes", package, version, size);
    return 0;
}

void pouch_serial_fw_url_abort(void)
{
    init_once();
    pouch_mutex_lock(&handoff.lock, POUCH_FOREVER);
    handoff.active = false;
    handoff.signed_ok = false;
    handoff.rec_len = 0;
    handoff.rec_sent = 0;
    pouch_mutex_unlock(&handoff.lock);
    POUCH_LOG_WRN("Firmware handoff withdrawn");
}

bool pouch_serial_fw_url_active(void)
{
    init_once();
    pouch_mutex_lock(&handoff.lock, POUCH_FOREVER);
    bool active = handoff.active;
    pouch_mutex_unlock(&handoff.lock);
    return active;
}

/*
 * Sign the pending URL into the record. Called with the lock held.
 *
 * Signing happens here rather than in begin() because a signed URL is valid for
 * a short window: one signed when the manifest arrived may well have expired by
 * the time the host asks for it, and the host may not ask for several sessions.
 */
static bool sign_pending(void)
{
    if (!signer)
    {
        POUCH_LOG_WRN("No URL signer installed, cannot hand off firmware");
        return false;
    }

    char *dst = (char *) &handoff.rec[handoff.names_len];
    size_t cap = sizeof(handoff.rec) - handoff.names_len;
    size_t olen = 0;

    int err = signer(handoff.url, strlen(handoff.url), dst, cap, &olen);
    if (err != 0)
    {
        POUCH_LOG_WRN("Failed to sign artifact URL: %d", err);
        return false;
    }

    if (olen == 0 || olen > CONFIG_POUCH_SERIAL_FW_URL_MAX_LEN || olen > cap)
    {
        POUCH_LOG_WRN("Signed URL does not fit the record: %zu bytes", olen);
        return false;
    }

    pouch_serial_put_le16(&handoff.rec[42], (uint16_t) olen);
    handoff.rec_len = handoff.names_len + olen;
    handoff.rec_sent = 0;
    handoff.signed_ok = true;
    return true;
}

/* --- serial endpoint: signed artifact URL (device -> broker) --- */

static int fw_url_start(struct pouch_bearer *bearer)
{
    init_once();
    pouch_mutex_lock(&handoff.lock, POUCH_FOREVER);

    if (handoff.active && !handoff.signed_ok)
    {
        /* A failure here is not an error the session should carry: the host is
         * simply told there is nothing to collect, and the record stays pending
         * so the next poll signs it again. Returning an error would fail the
         * whole session over a clock that is not set yet.
         */
        (void) sign_pending();
    }

    pouch_mutex_unlock(&handoff.lock);
    return 0;
}

static enum pouch_result fw_url_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    uint8_t *out = dst;
    size_t cap = *dst_len;
    size_t produced = 0;

    init_once();
    pouch_mutex_lock(&handoff.lock, POUCH_FOREVER);

    /* Nothing announced, or nothing signable yet: an empty transfer says so,
     * and costs the host two frames.
     */
    if (!handoff.active || !handoff.signed_ok)
    {
        pouch_mutex_unlock(&handoff.lock);
        *dst_len = 0;
        return POUCH_NO_MORE_DATA;
    }

    while (produced < cap && handoff.rec_sent < handoff.rec_len)
    {
        out[produced++] = handoff.rec[handoff.rec_sent++];
    }

    bool done = handoff.rec_sent >= handoff.rec_len;
    if (done)
    {
        /* Handed over. The host owns the fetch from here, and reports back on
         * the verdict channel.
         */
        handoff.active = false;
        handoff.signed_ok = false;
    }

    pouch_mutex_unlock(&handoff.lock);

    *dst_len = produced;

    if (done)
    {
        POUCH_LOG_INF("Signed artifact URL handed to the host");
        return POUCH_NO_MORE_DATA;
    }

    return POUCH_MORE_DATA;
}

const struct pouch_endpoint pouch_device_endpoint_fw_url = {
    .start = fw_url_start,
    .send = fw_url_send,
};

/* --- serial endpoint: wall-clock time (broker -> device) --- */

void pouch_serial_time_callback_set(pouch_serial_time_cb_t cb)
{
    wallclock.cb = cb;
}

bool pouch_serial_time_synced(void)
{
    return wallclock.synced;
}

static int time_start(struct pouch_bearer *bearer)
{
    wallclock.len = 0;
    return 0;
}

static int time_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    /* Anything longer than a time record is not one; keep what fits and let the
     * length check in time_end() reject it.
     */
    size_t space = sizeof(wallclock.buf) - wallclock.len;
    size_t n = len < space ? len : space;

    memcpy(&wallclock.buf[wallclock.len], buf, n);
    wallclock.len += n;

    return 0;
}

static void time_end(struct pouch_bearer *bearer, bool success)
{
    if (!success || wallclock.len != POUCH_SERIAL_TIME_LEN)
    {
        POUCH_LOG_DBG("Ignoring malformed time record (%zu bytes)", wallclock.len);
        return;
    }

    if (get_le32(&wallclock.buf[0]) != POUCH_SERIAL_TIME_MAGIC)
    {
        POUCH_LOG_WRN("Ignoring time record with bad magic");
        return;
    }

    int64_t seconds = 0;
    for (int i = 7; i >= 0; i--)
    {
        seconds = (seconds << 8) | wallclock.buf[4 + i];
    }

    wallclock.synced = true;

    POUCH_LOG_INF("Broker reported time: %lld", (long long) seconds);

    if (wallclock.cb)
    {
        wallclock.cb(seconds);
    }
}

const struct pouch_endpoint pouch_device_endpoint_time = {
    .start = time_start,
    .end = time_end,
    .recv = time_recv,
};
