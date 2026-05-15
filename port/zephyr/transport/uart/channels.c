/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/sys/util.h>
#include "protocol.h"
#include "transport/bearer.h"
#include "channels.h"

static enum serial_channel get_channel(struct pouch_bearer *bearer)
{
    struct channel *ch = CHANNEL_OF(bearer);
    return ch->ch;
}

int serial_bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len)
{
    return serial_send_data(get_channel(bearer), buf, len);
}

void serial_bearer_ready(struct pouch_bearer *bearer)
{
    // ?
}

void serial_bearer_close(struct pouch_bearer *bearer, bool success)
{
    serial_send_cmd(get_channel(bearer), SERIAL_CMD_CLOSE);
}
