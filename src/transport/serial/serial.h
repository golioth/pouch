/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <pouch/transport/serial/common.h>
#include "channel.h"

struct pouch_serial;
typedef void (*pouch_serial_ready_cb_t)(struct pouch_serial *serial);
typedef void (*pouch_serial_ch_closed_t)(struct pouch_serial *serial,
                                         enum pouch_serial_channel_id ch,
                                         bool success);

/**
 * Pouch serial instance, representing one serial bus that runs the Pouch Serial protocol.
 */
struct pouch_serial
{
    struct pouch_serial_channel channels[POUCH_SERIAL_CHANNEL_COUNT];
    pouch_serial_ready_cb_t ready;
    pouch_serial_ch_closed_t ch_closed;
};

void pouch_serial_init(struct pouch_serial *s,
                       pouch_serial_ready_cb_t ready,
                       pouch_serial_ch_closed_t ch_closed);

int pouch_serial_recv(struct pouch_serial *s, const void *frame, size_t len);

size_t pouch_serial_frame_get(struct pouch_serial *s, uint8_t *buf, size_t maxlen);
