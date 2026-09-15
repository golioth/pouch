/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <pouch/transport/serial/common.h>
#include "../endpoints/endpoint.h"
#include "../bearer.h"
#include "packet.h"
#include "channel.h"

#include <pouch/port.h>

POUCH_LOG_REGISTER(serial_channel, CONFIG_POUCH_SERIAL_LOG_LEVEL);

enum ch_flag
{
    CH_FLAG_OPEN,
    CH_FLAG_OPENING,
    CH_FLAG_PENDING,
    CH_FLAG_ERROR,
    CH_FLAG_SUSPENDED,
};

static enum pouch_serial_channel_id channel_id(const struct pouch_serial_channel *ch)
{
    return ch->id;
}

static bool is_receiver(const struct pouch_serial_channel *ch)
{
    return (ch->endpoint->send == NULL);
}

static void signal_ready(struct pouch_serial_channel *ch)
{
    if (ch->bearer.ready != NULL)
    {
        ch->bearer.ready(&ch->bearer);
    }
}

static int handle_ack(struct pouch_serial_channel *ch, bool err)
{
    POUCH_LOG_DBG("ACK on channel %d (err=%d)", channel_id(ch), err);
    if (err)
    {
        POUCH_LOG_ERR("Received NACK on channel %d", channel_id(ch));
        pouch_serial_ch_close(ch, false);
        return 0;
    }

    if (pouch_atomic_test_bit(&ch->flags, CH_FLAG_SUSPENDED))
    {
        return 0;
    }

    /* Receiver channels don't open via ACK - they open when the first DATA
     * frame arrives (handle_data).  Just respond with an ACK so the remote
     * sender knows it may proceed. */
    if (ch->endpoint->send == NULL)
    {
        signal_ready(ch);
        return 0;
    }

    /* First prompt: open the transfer. */
    return pouch_serial_ch_open(ch);
}

/*
 * Handle an incoming Data frame on a Broker->Device channel.
 * The broker is writing data to the device.
 */
static int handle_data(struct pouch_serial_channel *ch,
                       const struct pouch_serial_header *hdr,
                       const void *payload,
                       size_t len)
{
    POUCH_LOG_DBG("DATA on channel %d (len=%u, first=%d, last=%d, err=%d)",
                  channel_id(ch),
                  len,
                  hdr->first,
                  hdr->last,
                  hdr->err);
    if (hdr->err)
    {
        POUCH_LOG_WRN("ch %d: error flag set, closing", channel_id(ch));
        pouch_serial_ch_close(ch, false);
        return 0;
    }

    if (hdr->first)
    {
        if (pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
            && !pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING))
        {
            POUCH_LOG_ERR("ch %d: already open", channel_id(ch));
            return -EBUSY;
        }

        POUCH_LOG_DBG("ch %d: opening receiver channel", channel_id(ch));
        int err = pouch_serial_ch_open(ch);
        if (err)
        {
            return err;
        }

        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_OPENING);
    }
    else if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN))
    {
        POUCH_LOG_ERR("ch %d: not open", channel_id(ch));
        return -EINVAL;
    }

    if (len > 0)
    {
        if (ch->endpoint->recv == NULL)
        {
            POUCH_LOG_ERR("ch %d: recv on send endpoint", channel_id(ch));
            pouch_serial_ch_close(ch, false);
            return -EINVAL;
        }

        int err = ch->endpoint->recv(&ch->bearer, payload, len);
        if (err)
        {
            POUCH_LOG_ERR("ch %d: recv failed: %d", channel_id(ch), err);
            pouch_serial_ch_close(ch, false);
            return err;
        }
    }

    if (hdr->last)
    {
        POUCH_LOG_DBG("ch %d: last fragment, closing", channel_id(ch));
        pouch_serial_ch_close(ch, true);
    }

    return 0;
}

int pouch_serial_ch_recv(struct pouch_serial_channel *ch,
                         const struct pouch_serial_header *header,
                         const void *payload,
                         size_t len)
{
    int err;
    if (header->is_data)
    {
        err = handle_data(ch, header, payload, len);
    }
    else
    {
        err = handle_ack(ch, header->err);
    }

    if (err)
    {
        // Let the error response through, even if the channel isn't open
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_ERROR);
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
    }

    return err;
}

size_t pouch_serial_ch_frame_get(struct pouch_serial_channel *ch, uint8_t *buf, size_t maxlen)
{

    if (maxlen <= POUCH_SERIAL_HEADER_LEN)
    {
        return -EINVAL;
    }

    if (!pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_PENDING))
    {
        return 0;
    }

    if (pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_ERROR))
    {
        struct pouch_serial_header hdr = {
            .channel = channel_id(ch),
            .is_data = !is_receiver(ch),
            .err = true,
            .first = pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_OPENING),
            .last = true,
        };

        *buf = pouch_serial_header_encode(&hdr);
        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_OPEN);
        return POUCH_SERIAL_HEADER_LEN;
    }

    if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN))
    {
        return 0;
    }

    // If this is a receiver channel
    if (is_receiver(ch))
    {
        struct pouch_serial_header hdr = {
            .is_data = false,
            .err = pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_ERROR),
            .channel = channel_id(ch),
        };
        *buf = pouch_serial_header_encode(&hdr);
        POUCH_LOG_DBG("ch %d: sending ACK (err=%d)", channel_id(ch), hdr.err);
        return POUCH_SERIAL_HEADER_LEN;
    }


    size_t len = maxlen - POUCH_SERIAL_HEADER_LEN;
    enum pouch_result result = ch->endpoint->send(&ch->bearer, &buf[POUCH_SERIAL_HEADER_LEN], &len);
    if (len == 0 && result == POUCH_MORE_DATA)
    {
        // Endpoint has no data ready to send, but expects to be called again later.
        // Don't send an empty frame. CH_FLAG_PENDING was consumed on entry, so re-arm
        // it here: nothing else will, and the channel would stay un-pending forever.
        pouch_serial_ch_ready(ch);
        return 0;
    }

    bool error = (result == POUCH_ERROR);
    bool is_last = (result == POUCH_NO_MORE_DATA) || error;

    struct pouch_serial_header hdr = {
        .channel = channel_id(ch),
        .is_data = true,
        .err = error,
        .first = pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_OPENING),
        .last = is_last,
    };

    *buf = pouch_serial_header_encode(&hdr);

    if (is_last)
    {
        POUCH_LOG_DBG("ch %d: last fragment, closing", channel_id(ch));
        pouch_serial_ch_close(ch, !error);
    }
    else
    {
        signal_ready(ch);
    }

    return POUCH_SERIAL_HEADER_LEN + len;
}

void pouch_serial_ch_ready(struct pouch_serial_channel *ch)
{
    POUCH_LOG_DBG("ch %d: is ready to send", channel_id(ch));
    if (ch->endpoint->send != NULL || pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING))
    {
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
    }
}

int pouch_serial_ch_open(struct pouch_serial_channel *ch)
{
    if (pouch_atomic_test_and_set_bit(&ch->flags, CH_FLAG_OPEN))
    {
        return 0;
    }

    POUCH_LOG_DBG("ch %d: opening", channel_id(ch));
    int ret = ch->endpoint->start(&ch->bearer);
    if (ret)
    {
        POUCH_LOG_ERR("Endpoint start() failed for channel %d: %d", channel_id(ch), ret);
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_ERROR);
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
        signal_ready(ch);
        return ret;
    }

    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_ERROR);
    pouch_atomic_set_bit(&ch->flags, CH_FLAG_OPENING);
    pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
    signal_ready(ch);
    return ret;
}

void pouch_serial_ch_close(struct pouch_serial_channel *ch, bool success)
{
    if (!pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_OPEN))
    {
        return;
    }

    if (!success)
    {
        // close the channel, but allow an error frame to go through.
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_ERROR);
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
    }

    if (ch->endpoint->end != NULL)
    {
        ch->endpoint->end(&ch->bearer, success);
    }

    if (ch->closed_cb != NULL)
    {
        ch->closed_cb(ch, success);
    }
}

bool pouch_serial_ch_is_open(struct pouch_serial_channel *ch)
{
    return pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN);
}

bool pouch_serial_ch_has_error(struct pouch_serial_channel *ch)
{
    return pouch_atomic_test_bit(&ch->flags, CH_FLAG_ERROR);
}

void pouch_serial_ch_suspend(struct pouch_serial_channel *ch, bool suspend)
{
    if (suspend)
    {
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_SUSPENDED);
    }
    else
    {
        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_SUSPENDED);
        signal_ready(ch);
    }
}
