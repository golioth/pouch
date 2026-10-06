/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../bearer.h"
#include "packet.h"

#include <pouch/port.h>

struct pouch_serial_channel;
struct pouch_endpoint;

typedef void (*pouch_serial_ch_closed_cb_t)(struct pouch_serial_channel *ch, bool success);

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
    CH_FLAG_COUNT,
};

struct pouch_serial_channel
{
    struct pouch_bearer bearer;
    const struct pouch_endpoint *endpoint;
    pouch_serial_ch_closed_cb_t closed_cb;
    POUCH_ATOMIC_DEFINE(flags, CH_FLAG_COUNT);
    uint8_t id;
    /** Sender only: the last Data frame, kept until it's ACKed, so it can be sent again. */
    uint8_t frame[CONFIG_POUCH_SERIAL_FRAME_MAX_LEN];
    size_t frame_len;
};

int pouch_serial_ch_recv(struct pouch_serial_channel *ch,
                         const struct pouch_serial_header *header,
                         const void *payload,
                         size_t len);

size_t pouch_serial_ch_frame_get(struct pouch_serial_channel *ch, uint8_t *buf, size_t maxlen);

void pouch_serial_ch_ready(struct pouch_serial_channel *ch);
int pouch_serial_ch_open(struct pouch_serial_channel *ch);
void pouch_serial_ch_close(struct pouch_serial_channel *ch, bool success);
bool pouch_serial_ch_is_open(struct pouch_serial_channel *ch);
bool pouch_serial_ch_has_error(struct pouch_serial_channel *ch);

void pouch_serial_ch_suspend(struct pouch_serial_channel *ch, bool suspend);

/**
 * Check whether the channel is waiting for a response from the remote.
 *
 * A sender is waiting for an ACK to its last frame, and a receiver is waiting for the next frame
 * after an ACK.
 */
bool pouch_serial_ch_is_waiting(struct pouch_serial_channel *ch);

/**
 * Check whether the channel is a receiver waiting for the first frame of a transfer. The remote
 * might not have any data yet, so this isn't necessarily a failure.
 */
bool pouch_serial_ch_is_awaiting_first(struct pouch_serial_channel *ch);

/**
 * Retry the last exchange on a channel that's waiting for a response.
 *
 * A sender sends its last frame again, and a receiver sends a NACK, asking the remote to send its
 * last frame again.
 *
 * @return Whether a retry was queued.
 */
bool pouch_serial_ch_retry(struct pouch_serial_channel *ch);

static inline struct pouch_serial_channel *pouch_serial_ch_from_bearer(struct pouch_bearer *bearer)
{
    return CONTAINER_OF(bearer, struct pouch_serial_channel, bearer);
}
