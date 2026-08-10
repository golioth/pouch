/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <pouch/port.h>
#include <pouch/transport/serial/fw.h>
#include <pouch/types.h>

#include "endpoints.h"

POUCH_LOG_REGISTER(pouch_fw_relay, CONFIG_POUCH_COMMON_LOG_LEVEL);

/*
 * Device-side firmware relay.
 *
 * The application writes image bytes in from its OTA component callback; the
 * serial firmware channel drains them out. Only CONFIG_POUCH_SERIAL_FW_BUF_SIZE
 * bytes of the image are ever resident - writes block while the buffer is full,
 * which is what throttles the OTA download to the speed of the serial link.
 *
 * A relay is framed as one serial transfer: a header describing the image
 * followed by the image bytes. When no update is in flight the endpoint reports
 * an immediately-empty transfer so the broker's poll costs two frames.
 */

/*
 * The buffer only has to cover the gap between the downlink filling it and the
 * firmware channel draining it. The broker collects this channel concurrently
 * with the downlink that feeds it, so the two run at the same time and the
 * image is never resident.
 */
#ifndef CONFIG_POUCH_SERIAL_FW_BUF_SIZE
#define CONFIG_POUCH_SERIAL_FW_BUF_SIZE 8192
#endif

#define MAX_NAME_LEN 32

struct fw_relay
{
    pouch_mutex_t lock;
    pouch_sem_t space; /* given when the drain frees buffer space */

    uint8_t hdr[POUCH_SERIAL_FW_HDR_FIXED_LEN + 2 * MAX_NAME_LEN];
    size_t hdr_len;
    size_t hdr_sent; /* bytes of the current chunk's header already sent */

    uint32_t img_size;  /* total image size */
    uint32_t sent;      /* absolute offset of the next byte to send */
    bool chunk_open;    /* a chunk transfer is in progress */

    uint8_t buf[CONFIG_POUCH_SERIAL_FW_BUF_SIZE];
    size_t head; /* write index */
    size_t tail; /* read index */
    size_t used;

    bool active;   /* a relay has been announced */
    bool complete; /* the application has written the last byte */

    bool status_valid;
    enum pouch_serial_fw_status status;
};

static struct fw_relay relay;
static bool inited;

static void relay_init_once(void)
{
    if (!inited)
    {
        pouch_mutex_init(&relay.lock);
        pouch_sem_init(&relay.space, 0, 1);
        inited = true;
    }
}

static void put_le32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t) (v & 0xff);
    dst[1] = (uint8_t) ((v >> 8) & 0xff);
    dst[2] = (uint8_t) ((v >> 16) & 0xff);
    dst[3] = (uint8_t) ((v >> 24) & 0xff);
}

int pouch_serial_fw_begin(const char *package,
                          const char *version,
                          uint32_t size,
                          const uint8_t sha256[32])
{
    if (!package || !version || !sha256 || size == 0)
    {
        return -EINVAL;
    }

    size_t pkg_len = strlen(package);
    size_t ver_len = strlen(version);
    if (pkg_len > MAX_NAME_LEN || ver_len > MAX_NAME_LEN)
    {
        return -EINVAL;
    }

    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);

    if (relay.active)
    {
        pouch_mutex_unlock(&relay.lock);
        return -EBUSY;
    }

    /* The offset field is rewritten per chunk in fw_send(). */
    uint8_t *h = relay.hdr;
    put_le32(&h[0], POUCH_SERIAL_FW_MAGIC);
    put_le32(&h[4], size);
    put_le32(&h[8], 0);
    memcpy(&h[12], sha256, 32);
    h[44] = (uint8_t) ver_len;
    h[45] = (uint8_t) pkg_len;
    memcpy(&h[POUCH_SERIAL_FW_HDR_FIXED_LEN], version, ver_len);
    memcpy(&h[POUCH_SERIAL_FW_HDR_FIXED_LEN + ver_len], package, pkg_len);

    relay.hdr_len = POUCH_SERIAL_FW_HDR_FIXED_LEN + ver_len + pkg_len;
    relay.hdr_sent = 0;
    relay.head = 0;
    relay.tail = 0;
    relay.used = 0;
    relay.img_size = size;
    relay.sent = 0;
    relay.chunk_open = false;
    relay.complete = false;
    relay.status_valid = false;
    relay.active = true;

    pouch_mutex_unlock(&relay.lock);

    POUCH_LOG_INF("Firmware relay started: %s %s, %u bytes", package, version, size);
    return 0;
}

int pouch_serial_fw_write(const void *buf, size_t len, pouch_timeout_t timeout)
{
    const uint8_t *src = buf;
    size_t written = 0;

    if (!buf)
    {
        return -EINVAL;
    }

    relay_init_once();

    while (written < len)
    {
        pouch_mutex_lock(&relay.lock, POUCH_FOREVER);

        if (!relay.active)
        {
            pouch_mutex_unlock(&relay.lock);
            return -EPIPE;
        }

        size_t space = sizeof(relay.buf) - relay.used;
        size_t n = len - written;
        if (n > space)
        {
            n = space;
        }

        for (size_t i = 0; i < n; i++)
        {
            relay.buf[relay.head] = src[written + i];
            relay.head = (relay.head + 1) % sizeof(relay.buf);
        }
        relay.used += n;
        written += n;

        pouch_mutex_unlock(&relay.lock);

        if (written < len)
        {
            /* Buffer is full - wait for the serial channel to drain some. */
            if (pouch_sem_take(&relay.space, timeout) != 0)
            {
                return written > 0 ? (int) written : -EAGAIN;
            }
        }
    }

    return (int) written;
}

void pouch_serial_fw_end(void)
{
    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);
    relay.complete = true;
    pouch_mutex_unlock(&relay.lock);
    POUCH_LOG_DBG("Firmware relay: image complete");
}

void pouch_serial_fw_abort(void)
{
    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);
    relay.active = false;
    relay.chunk_open = false;
    relay.complete = false;
    relay.used = 0;
    relay.head = 0;
    relay.tail = 0;
    relay.sent = 0;
    relay.hdr_sent = 0;
    pouch_mutex_unlock(&relay.lock);
    POUCH_LOG_WRN("Firmware relay aborted");
}

bool pouch_serial_fw_active(void)
{
    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);
    bool active = relay.active;
    pouch_mutex_unlock(&relay.lock);
    return active;
}

bool pouch_serial_fw_pressure(void)
{
    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);
    bool full = relay.active && (relay.used >= (sizeof(relay.buf) / 2));
    pouch_mutex_unlock(&relay.lock);
    return full;
}

bool pouch_serial_fw_status_get(enum pouch_serial_fw_status *status)
{
    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);
    bool valid = relay.status_valid;
    if (valid && status)
    {
        *status = relay.status;
    }
    pouch_mutex_unlock(&relay.lock);
    return valid;
}

/* --- serial endpoint: firmware stream (device -> broker) --- */

static int fw_start(struct pouch_bearer *bearer)
{
    relay_init_once();
    return 0;
}

static enum pouch_result fw_send(struct pouch_bearer *bearer, void *dst, size_t *dst_len)
{
    uint8_t *out = dst;
    size_t cap = *dst_len;
    size_t produced = 0;

    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);

    /* No relay in flight: an empty transfer tells the broker there is nothing
     * to collect, which is the normal answer and costs two frames.
     */
    if (!relay.active)
    {
        pouch_mutex_unlock(&relay.lock);
        *dst_len = 0;
        return POUCH_NO_MORE_DATA;
    }

    /* A relay is in flight but nothing is buffered yet. Hold the transfer open
     * and ask to be called again rather than ending it: the broker collects
     * this channel alongside the downlink that feeds it, so "empty right now"
     * means "the next downlink block has not been decrypted yet", not "done".
     * Ending the transfer here would finish the collection before any image
     * bytes existed.
     */
    if (relay.used == 0 && !relay.complete)
    {
        pouch_mutex_unlock(&relay.lock);
        *dst_len = 0;
        return POUCH_MORE_DATA;
    }

    if (!relay.chunk_open)
    {
        /* Start a chunk: stamp it with the absolute offset it begins at. */
        put_le32(&relay.hdr[8], relay.sent);
        relay.hdr_sent = 0;
        relay.chunk_open = true;
    }

    while (produced < cap && relay.hdr_sent < relay.hdr_len)
    {
        out[produced++] = relay.hdr[relay.hdr_sent++];
    }

    while (produced < cap && relay.used > 0)
    {
        out[produced++] = relay.buf[relay.tail];
        relay.tail = (relay.tail + 1) % sizeof(relay.buf);
        relay.used--;
        relay.sent++;
    }

    /* The transfer stays open until the whole image has gone out. Running the
     * buffer dry only means the downlink has not caught up yet - the broker is
     * collecting this channel alongside the downlink that feeds it, so more
     * bytes are still coming within this same session.
     */
    bool drained = (relay.hdr_sent >= relay.hdr_len) && (relay.used == 0);
    bool image_done = drained && relay.complete && (relay.sent >= relay.img_size);

    if (image_done)
    {
        relay.chunk_open = false;
        relay.active = false;
    }

    uint32_t sent_now = relay.sent;
    uint32_t total = relay.img_size;
    pouch_mutex_unlock(&relay.lock);

    /* Wake a writer blocked on a full buffer. */
    pouch_sem_give(&relay.space);

    *dst_len = produced;

    if (image_done)
    {
        POUCH_LOG_INF("Firmware relay complete: %u bytes", sent_now);
        return POUCH_NO_MORE_DATA;
    }

    if (drained)
    {
        POUCH_LOG_DBG("Firmware relay caught up, %u/%u bytes relayed", sent_now, total);
    }

    return POUCH_MORE_DATA;
}

const struct pouch_endpoint pouch_device_endpoint_fw = {
    .start = fw_start,
    .send = fw_send,
};

/* --- serial endpoint: apply status (broker -> device) --- */

static int fw_status_start(struct pouch_bearer *bearer)
{
    return 0;
}

static int fw_status_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    const uint8_t *in = buf;

    if (len == 0)
    {
        return 0;
    }

    relay_init_once();
    pouch_mutex_lock(&relay.lock, POUCH_FOREVER);
    relay.status = (enum pouch_serial_fw_status) in[0];
    relay.status_valid = true;
    pouch_mutex_unlock(&relay.lock);

    POUCH_LOG_INF("Firmware apply status from broker: %u", in[0]);
    return 0;
}

const struct pouch_endpoint pouch_device_endpoint_fw_status = {
    .start = fw_status_start,
    .recv = fw_status_recv,
};
