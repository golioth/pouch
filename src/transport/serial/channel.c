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

enum ch_flag
{
    CH_FLAG_OPEN,
    CH_FLAG_FIRST_FRAGMENT,
    CH_FLAG_PENDING,
    CH_FLAG_ERROR,
};

static enum pouch_serial_channel_id channel_id(const struct pouch_serial_channel *ch)
{
    return ch->id;
}

static int handle_ack(struct pouch_serial_channel *ch, bool err)
{
    if (err)
    {
        pouch_serial_ch_close(ch, false);
        return 0;
    }

    /* Receiver channels don't open via ACK - they open when the first DATA
     * frame arrives (handle_data).  Just respond with an ACK so the remote
     * sender knows it may proceed. */
    if (ch->endpoint->send == NULL)
    {
        pouch_serial_ch_ready(ch);
        return 0;
    }

    /* First prompt: open the transfer. */
    if (!pouch_atomic_test_and_set_bit(&ch->flags, CH_FLAG_OPEN))
    {
        int ret = ch->endpoint->start(&ch->bearer);
        if (ret)
        {
            pouch_atomic_set_bit(&ch->flags, CH_FLAG_ERROR);
            pouch_serial_ch_close(ch, false);
        }

        pouch_atomic_set_bit(&ch->flags, CH_FLAG_FIRST_FRAGMENT);
        pouch_serial_ch_ready(ch);
    }

    return 0;
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
    if (hdr->err)
    {
        pouch_serial_ch_close(ch, false);
        return 0;
    }

    if (!pouch_atomic_test_and_set_bit(&ch->flags, CH_FLAG_OPEN))
    {
        if (!hdr->first)
        {
            pouch_serial_ch_close(ch, false);
            return -EINVAL;
        }

        int err = ch->endpoint->start(&ch->bearer);
        if (err)
        {
            pouch_atomic_clear_bit(&ch->flags, CH_FLAG_OPEN);
            pouch_atomic_set_bit(&ch->flags, CH_FLAG_ERROR);
            pouch_serial_ch_ready(ch);  // nack
            return err;
        }

        pouch_atomic_set_bit(&ch->flags, CH_FLAG_FIRST_FRAGMENT);
    }
    else if (hdr->first)
    {
        // Unexpected first fragment in an already-open channel
        pouch_serial_ch_close(ch, false);
        return -EINVAL;
    }

    if (len > 0)
    {
        int err = ch->endpoint->recv(&ch->bearer, payload, len);
        if (err)
        {
            pouch_serial_ch_close(ch, false);
            return err;
        }
    }

    if (hdr->last)
    {
        pouch_serial_ch_close(ch, true);
    }

    return 0;
}

int pouch_serial_ch_recv(struct pouch_serial_channel *ch,
                         const struct pouch_serial_header *header,
                         const void *payload,
                         size_t len)
{
    /* A channel with no endpoint is not configured on this build (e.g. the
     * certificate channels when SAEAD encryption is disabled). Ignore any
     * frames that arrive for it rather than dereferencing a NULL endpoint. */
    if (ch->endpoint == NULL)
    {
        return 0;
    }

    if (header->is_data)
    {
        return handle_data(ch, header, payload, len);
    }

    return handle_ack(ch, header->err);
}


size_t pouch_serial_ch_frame_get(struct pouch_serial_channel *ch, uint8_t *buf, size_t maxlen)
{
    if (!pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_PENDING))
    {
        return 0;
    }

    // If this is a receiver channel
    if (ch->endpoint->send == NULL)
    {
        struct pouch_serial_header hdr = {
            .is_data = false,
            .err = pouch_atomic_test_bit(&ch->flags, CH_FLAG_ERROR),
            .channel = channel_id(ch),
        };
        *buf = pouch_serial_header_encode(&hdr);

        return POUCH_SERIAL_HEADER_LEN;
    }

    if (pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_ERROR))
    {
        struct pouch_serial_header hdr = {
            .channel = channel_id(ch),
            .is_data = true,
            .err = true,
            .first = pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_FIRST_FRAGMENT),
            .last = true,
        };

        *buf = pouch_serial_header_encode(&hdr);
        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_OPEN);
        return POUCH_SERIAL_HEADER_LEN;
    }

    if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN))
    {
        /* Sender is not yet open - produce an ACK prompt to request the
         * remote side to open the channel (by ACK-ing back). */
        struct pouch_serial_header hdr = {
            .is_data = false,
            .err = false,
            .channel = channel_id(ch),
        };
        *buf = pouch_serial_header_encode(&hdr);

        return POUCH_SERIAL_HEADER_LEN;
    }

    size_t len = maxlen - POUCH_SERIAL_HEADER_LEN;
    enum pouch_result result = ch->endpoint->send(&ch->bearer, &buf[POUCH_SERIAL_HEADER_LEN], &len);
    if (len == 0 && result == POUCH_MORE_DATA)
    {
        // Endpoint has no data ready to send, but expects to be called again later.
        // Don't send an empty frame; just wait for the next call.
        return 0;
    }

    bool error = (result == POUCH_ERROR);
    bool is_last = (result == POUCH_NO_MORE_DATA) || error;

    struct pouch_serial_header hdr = {
        .channel = channel_id(ch),
        .is_data = true,
        .err = error,
        .first = pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_FIRST_FRAGMENT),
        .last = is_last,
    };

    *buf = pouch_serial_header_encode(&hdr);

    if (is_last)
    {
        pouch_serial_ch_close(ch, !error);
    }
    else
    {
        pouch_serial_ch_ready(ch);
    }

    return POUCH_SERIAL_HEADER_LEN + len;
}

void pouch_serial_ch_ready(struct pouch_serial_channel *ch)
{
    pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
}

void pouch_serial_ch_close(struct pouch_serial_channel *ch, bool success)
{
    if (!pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_OPEN))
    {
        return;
    }

    if (!success)
    {
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_ERROR);
    }

    ch->endpoint->end(&ch->bearer, success);
    if (ch->closed_cb)
    {
        ch->closed_cb(ch, success);
    }

    if (!success)
    {
        pouch_serial_ch_ready(ch);
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
