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

#include <string.h>

POUCH_LOG_REGISTER(serial_channel, CONFIG_POUCH_SERIAL_LOG_LEVEL);

enum ch_flag
{
    CH_FLAG_OPEN,
    /** Opened, but no fragment has been sent or received yet. */
    CH_FLAG_OPENING,
    /** A frame should be produced on the next call to pouch_serial_ch_frame_get(). */
    CH_FLAG_PENDING,
    CH_FLAG_ERROR,
    CH_FLAG_SUSPENDED,
    /** Sender only: a fragment has been sent, and the remote hasn't ACKed it yet. */
    CH_FLAG_WAIT_ACK,
    /** Sender only: the fragment waiting for an ACK is the last one. */
    CH_FLAG_LAST_SENT,
    /** Sender only: the fragment waiting for an ACK should be sent again. */
    CH_FLAG_RESEND,
    /** Receiver only: the next ACK should be a NACK, asking the sender to send its frame again. */
    CH_FLAG_NACK,
    /**
     * Receiver: the first fragment of the transfer has been requested.
     * Sender: the remote requested a transfer while the channel was suspended.
     */
    CH_FLAG_REQUESTED,
    /** Receiver only: the last transfer was completed successfully. */
    CH_FLAG_COMPLETED,
    /**
     * Receiver only: the transfer was opened while an ACK was owed to the previous one, so the
     * first fragment has to be requested with a separate ACK.
     */
    CH_FLAG_REQUEST_NEXT,
};

/*
 * The serial protocol is stop-and-wait: every fragment is paired with an ACK from the receiver,
 * and the sender keeps the fragment until it's ACKed. As the broker initiates all exchanges, the
 * pairing depends on the channel direction:
 *
 * - Device -> Broker channels are pulled by the broker: the receiver sends an ACK to request each
 *   fragment, and the sender responds with exactly one fragment per ACK. The broker requests the
 *   first fragment once, and the device holds on to the request until it has something to send.
 * - Broker -> Device channels are pushed by the broker: the sender sends a fragment, and waits for
 *   the receiver to ACK it before sending the next one.
 *
 * In both directions, the last fragment is ACKed too, and the sender doesn't consider the transfer
 * complete until it is.
 *
 * A receiver that didn't get a fragment, or got a broken one, sends a NACK, and the sender sends
 * the fragment again. Only the broker can detect a lost frame, as the device can't send anything
 * on its own, so the broker drives retries with pouch_serial_ch_retry(). There are no sequence
 * numbers, so a fragment sent again after its ACK was lost is received twice. The receiver
 * recognizes this for the last fragment of a transfer, which it just ACKs again.
 */

static enum pouch_serial_channel_id channel_id(const struct pouch_serial_channel *ch)
{
    return ch->id;
}

static bool is_receiver(const struct pouch_serial_channel *ch)
{
    return (ch->endpoint->send == NULL);
}

static bool is_push(const struct pouch_serial_channel *ch)
{
    return POUCH_SERIAL_CH_BROKER_TO_DEVICE(channel_id(ch));
}

static void signal_ready(struct pouch_serial_channel *ch)
{
    if (ch->bearer.ready != NULL)
    {
        ch->bearer.ready(&ch->bearer);
    }
}

/*
 * Close the channel. If the transfer failed and notify_remote is set, an error frame is sent to
 * the remote on the next call to pouch_serial_ch_frame_get().
 */
static void close_channel(struct pouch_serial_channel *ch, bool success, bool notify_remote)
{
    if (!pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_OPEN))
    {
        return;
    }

    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_WAIT_ACK);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_LAST_SENT);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_RESEND);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_NACK);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_REQUEST_NEXT);
    ch->frame_len = 0;

    if (!success)
    {
        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_COMPLETED);
    }

    if (!success && notify_remote)
    {
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

static int handle_ack(struct pouch_serial_channel *ch, const struct pouch_serial_header *hdr)
{
    POUCH_LOG_DBG("ACK on channel %d (err=%d, last=%d)", channel_id(ch), hdr->err, hdr->last);
    if (hdr->err && hdr->last)
    {
        POUCH_LOG_ERR("ch %d: transfer aborted by the remote", channel_id(ch));
        // The remote has already abandoned the transfer, so there's no need to tell it
        close_channel(ch, false, false);
        return 0;
    }

    if (is_receiver(ch))
    {
        POUCH_LOG_DBG("ch %d: ignoring ACK on receiver channel", channel_id(ch));
        return 0;
    }

    if (pouch_atomic_test_bit(&ch->flags, CH_FLAG_SUSPENDED))
    {
        if (!is_push(ch) && !pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN))
        {
            // The request is only made once, so hold on to it until the channel is resumed
            pouch_atomic_set_bit(&ch->flags, CH_FLAG_REQUESTED);
        }

        return 0;
    }

    if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN))
    {
        if (is_push(ch))
        {
            // Push senders are opened locally, not by the remote.
            POUCH_LOG_DBG("ch %d: ignoring ACK on closed channel", channel_id(ch));
            return 0;
        }

        // The first request from the remote opens the transfer, whether it's an ACK or a NACK
        return pouch_serial_ch_open(ch);
    }

    if (hdr->err)
    {
        if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_WAIT_ACK) || ch->frame_len == 0)
        {
            // We haven't sent anything since the last ACK, so there's nothing to send again
            return 0;
        }

        POUCH_LOG_WRN("ch %d: NACK, sending the last fragment again", channel_id(ch));
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_RESEND);
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
        signal_ready(ch);
        return 0;
    }

    if (!pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_WAIT_ACK))
    {
        // We haven't sent anything since the last ACK, so this one is redundant.
        return 0;
    }

    if (pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_LAST_SENT))
    {
        POUCH_LOG_DBG("ch %d: last fragment ACKed, closing", channel_id(ch));
        close_channel(ch, true, false);
        return 0;
    }

    signal_ready(ch);
    return 0;
}

/*
 * Handle an incoming Data frame. Only receiver channels accept Data frames.
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
        // The remote has already abandoned the transfer, so there's no need to tell it
        close_channel(ch, false, false);
        return 0;
    }

    if (!is_receiver(ch) || ch->endpoint->recv == NULL)
    {
        POUCH_LOG_ERR("ch %d: DATA on sender channel", channel_id(ch));
        close_channel(ch, false, true);
        return -EINVAL;
    }

    if (hdr->first)
    {
        if (pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
            && !pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING))
        {
            POUCH_LOG_ERR("ch %d: already open", channel_id(ch));
            return -EBUSY;
        }

        if (!is_push(ch) && !pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN))
        {
            // Pulled transfers are opened locally, and must be requested before they're sent
            POUCH_LOG_ERR("ch %d: unrequested transfer", channel_id(ch));
            return -EINVAL;
        }

        POUCH_LOG_DBG("ch %d: opening receiver channel", channel_id(ch));
        int err = pouch_serial_ch_open(ch);
        if (err)
        {
            return err;
        }

        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_OPENING);
    }
    else if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
             || pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING))
    {
        if (hdr->last && !pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
            && pouch_atomic_test_bit(&ch->flags, CH_FLAG_COMPLETED))
        {
            // The ACK to the last fragment was lost, and the sender sent it again
            POUCH_LOG_WRN("ch %d: last fragment received again, ACKing it again", channel_id(ch));
            pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
            return 0;
        }

        POUCH_LOG_ERR("ch %d: missing FIRST fragment", channel_id(ch));
        return -EINVAL;
    }

    if (len > 0)
    {
        int err = ch->endpoint->recv(&ch->bearer, payload, len);
        if (err)
        {
            POUCH_LOG_ERR("ch %d: recv failed: %d", channel_id(ch), err);
            close_channel(ch, false, true);
            return err;
        }
    }

    // Every fragment is ACKed, including the last one
    pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);

    if (hdr->last)
    {
        POUCH_LOG_DBG("ch %d: last fragment, closing", channel_id(ch));
        // Set before closing, as the channel may be opened again from the closed callback
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_COMPLETED);
        close_channel(ch, true, false);
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
        err = handle_ack(ch, header);
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
    // Frames are kept until they're ACKed, so they can't be larger than the buffer
    maxlen = MIN(maxlen, sizeof(ch->frame));
    if (maxlen <= POUCH_SERIAL_HEADER_LEN)
    {
        return 0;
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
        return POUCH_SERIAL_HEADER_LEN;
    }

    // Receivers only ever send ACKs, and may owe one after the channel has closed.
    if (is_receiver(ch))
    {
        struct pouch_serial_header hdr = {
            .is_data = false,
            .err = pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_NACK),
            .channel = channel_id(ch),
        };

        if (pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_REQUEST_NEXT))
        {
            // This ACK is owed to the previous transfer, request the first fragment separately
            pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
        }
        else if (!hdr.err && pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING))
        {
            pouch_atomic_set_bit(&ch->flags, CH_FLAG_REQUESTED);
        }

        *buf = pouch_serial_header_encode(&hdr);
        POUCH_LOG_DBG("ch %d: sending %s", channel_id(ch), hdr.err ? "NACK" : "ACK");
        return POUCH_SERIAL_HEADER_LEN;
    }

    if (pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_RESEND))
    {
        if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
            || !pouch_atomic_test_bit(&ch->flags, CH_FLAG_WAIT_ACK) || ch->frame_len > maxlen)
        {
            return 0;
        }

        memcpy(buf, ch->frame, ch->frame_len);
        return ch->frame_len;
    }

    if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
        || pouch_atomic_test_bit(&ch->flags, CH_FLAG_WAIT_ACK))
    {
        return 0;
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

    if (error)
    {
        // The frame we're returning already carries the error.
        close_channel(ch, false, false);
        return POUCH_SERIAL_HEADER_LEN;
    }

    if (is_last)
    {
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_LAST_SENT);
    }

    // Keep the frame until it's ACKed, in case it has to be sent again
    ch->frame_len = POUCH_SERIAL_HEADER_LEN + len;
    memcpy(ch->frame, buf, ch->frame_len);
    pouch_atomic_set_bit(&ch->flags, CH_FLAG_WAIT_ACK);

    return ch->frame_len;
}

void pouch_serial_ch_ready(struct pouch_serial_channel *ch)
{
    POUCH_LOG_DBG("ch %d: is ready to send", channel_id(ch));

    /* Senders may send whenever they're not waiting for an ACK. Receivers only send on their own
     * accord to request the first fragment of a pulled transfer, once - every other ACK is sent in
     * response to a fragment, or as a retry.
     */
    bool ready = is_receiver(ch) ? (pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING)
                                    && !pouch_atomic_test_bit(&ch->flags, CH_FLAG_REQUESTED))
                                 : !pouch_atomic_test_bit(&ch->flags, CH_FLAG_WAIT_ACK);
    if (ready)
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
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_WAIT_ACK);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_LAST_SENT);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_RESEND);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_NACK);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_REQUESTED);
    pouch_atomic_clear_bit(&ch->flags, CH_FLAG_COMPLETED);
    ch->frame_len = 0;

    if (is_receiver(ch) && pouch_atomic_test_bit(&ch->flags, CH_FLAG_PENDING))
    {
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_REQUEST_NEXT);
    }

    int ret = ch->endpoint->start(&ch->bearer);
    if (ret)
    {
        POUCH_LOG_ERR("Endpoint start() failed for channel %d: %d", channel_id(ch), ret);
        // The endpoint never started, so the channel never opened. Only notify the remote.
        pouch_atomic_clear_bit(&ch->flags, CH_FLAG_OPEN);
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
    close_channel(ch, success, true);
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

        // Act on a request that arrived while the channel was suspended
        if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
            && pouch_atomic_test_and_clear_bit(&ch->flags, CH_FLAG_REQUESTED))
        {
            pouch_serial_ch_open(ch);
            return;
        }

        signal_ready(ch);
    }
}

bool pouch_serial_ch_is_waiting(struct pouch_serial_channel *ch)
{
    if (!pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
        || pouch_atomic_test_bit(&ch->flags, CH_FLAG_PENDING))
    {
        // Closed, or we're about to send something ourselves
        return false;
    }

    // Receivers wait for the next fragment after every ACK
    return is_receiver(ch) || pouch_atomic_test_bit(&ch->flags, CH_FLAG_WAIT_ACK);
}

bool pouch_serial_ch_is_awaiting_first(struct pouch_serial_channel *ch)
{
    return is_receiver(ch) && pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPEN)
        && pouch_atomic_test_bit(&ch->flags, CH_FLAG_OPENING);
}

bool pouch_serial_ch_retry(struct pouch_serial_channel *ch)
{
    if (!pouch_serial_ch_is_waiting(ch))
    {
        return false;
    }

    if (is_receiver(ch))
    {
        POUCH_LOG_DBG("ch %d: retrying with a NACK", channel_id(ch));
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_NACK);
    }
    else
    {
        POUCH_LOG_DBG("ch %d: sending the last fragment again", channel_id(ch));
        pouch_atomic_set_bit(&ch->flags, CH_FLAG_RESEND);
    }

    pouch_atomic_set_bit(&ch->flags, CH_FLAG_PENDING);
    signal_ready(ch);
    return true;
}
