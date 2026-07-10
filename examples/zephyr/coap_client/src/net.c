/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/net/net_if.h>

#include "net.h"

#include "modem.h"
#include "wifi.h"

void net_connect(void)
{
    if (IS_ENABLED(CONFIG_WIFI))
    {
        wifi_connect();
    }
    else if (IS_ENABLED(CONFIG_MODEM))
    {
        modem_connect();
    }
}
