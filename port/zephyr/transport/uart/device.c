/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "channels.h"
#include "protocol.h"

static int bearer_send(struct pouch_bearer *bearer, const uint8_t *buf, size_t len);
static void bearer_ready(struct pouch_bearer *bearer);
static void bearer_close(struct pouch_bearer *bearer, bool success);

int pouch_serial_client_init(const struct device *device)
{
    return serial_init(device);
}

int pouch_serial_client_sync(void)
{
    return 0;
}
