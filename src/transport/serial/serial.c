/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <pouch/transport/serial/device.h>
#include "packet.h"
#include "serial.h"
#include "../bearer.h"
#include "../endpoints/device/endpoints.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pouch/port.h>

POUCH_LOG_REGISTER(pouch_serial, CONFIG_POUCH_SERIAL_LOG_LEVEL);

static struct pouch_serial_channel *channel_from_bearer(struct pouch_bearer *bearer)
{
    return CONTAINER_OF(bearer, struct pouch_serial_channel, bearer);
}

static struct pouch_serial *transport_from_channel(struct pouch_serial_channel *ch)
{
    return CONTAINER_OF(ch, struct pouch_serial, channels[ch->id]);
}

/*
 * Called by an endpoint when it has new data ready to send. Signal the broker
 * via the adapter's notify callback if one is registered.
 */
static void bearer_ready(struct pouch_bearer *bearer)
{
    struct pouch_serial_channel *ch = channel_from_bearer(bearer);
    struct pouch_serial *s = transport_from_channel(ch);

    pouch_serial_ch_ready(ch);
    if (s->ready)
    {
        s->ready(s);
    }
}

static void channel_closed(struct pouch_serial_channel *ch, bool success)
{
    struct pouch_serial *transport = transport_from_channel(ch);
    if (transport->ch_closed)
    {
        transport->ch_closed(transport, ch->id, success);
    }
}

/*
 * Called by an endpoint when it encounters an error and wants to abort the
 * current transfer. For Device->Broker channels, send an ERR|LAST Data frame
 * to notify the broker of the abort.
 */
static void bearer_close(struct pouch_bearer *bearer, bool success)
{
    pouch_serial_ch_close(pouch_serial_ch_from_bearer(bearer), success);
}

void pouch_serial_init(struct pouch_serial *s,
                       pouch_serial_ready_cb_t ready,
                       pouch_serial_ch_closed_t ch_closed)
{
    for (enum pouch_serial_channel_id i = 0; i < POUCH_SERIAL_CHANNEL_COUNT; i++)
    {
        s->channels[i].id = i;
        s->channels[i].flags = 0;
        s->channels[i].bearer.ready = bearer_ready;
        s->channels[i].bearer.close = bearer_close;
        s->channels[i].bearer.send = NULL;
        s->channels[i].closed_cb = channel_closed;
    }
    s->ready = ready;
    s->ch_closed = ch_closed;
}

int pouch_serial_recv(struct pouch_serial *s, const void *frame, size_t len)
{
    if (s == NULL)
    {
        POUCH_LOG_ERR("Received frame but transport is not initialized");
        return -EINVAL;
    }

    if (frame == NULL || len == 0)
    {
        return -EINVAL;
    }

    const uint8_t *bytes = frame;
    struct pouch_serial_header header;
    int err = pouch_serial_header_decode(bytes[0], &header);
    if (err)
    {
        POUCH_LOG_INF("malformed header 0x%02x, ignoring (len=%zu)", bytes[0], len);
        return err;
    }

    const void *payload = NULL;
    size_t payload_len = 0;
    if (len > POUCH_SERIAL_HEADER_LEN)
    {
        payload = &bytes[POUCH_SERIAL_HEADER_LEN];
        payload_len = len - POUCH_SERIAL_HEADER_LEN;
    }

    if (header.channel >= POUCH_SERIAL_CHANNEL_COUNT)
    {
        POUCH_LOG_WRN("unknown channel %u, ignoring", header.channel);
        return -EINVAL;
    }

    err = pouch_serial_ch_recv(&s->channels[header.channel], &header, payload, payload_len);
    if (err)
    {
        POUCH_LOG_ERR("failed to process frame for channel %u: %d", header.channel, err);
        return err;
    }

    return 0;
}

size_t pouch_serial_frame_get(struct pouch_serial *s, uint8_t *buf, size_t maxlen)
{
    for (enum pouch_serial_channel_id i = 0; i < POUCH_SERIAL_CHANNEL_COUNT; i++)
    {
        size_t len = pouch_serial_ch_frame_get(&s->channels[i], buf, maxlen);
        if (len > 0)
        {
            return len;
        }
    }

    return 0;
}
