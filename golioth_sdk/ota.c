/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota, LOG_LEVEL_DBG);

#include "dispatch.h"

static void ota_downlink(const void *data, size_t len, bool is_last)
{
    LOG_DBG("Received manifest");
}

GOLIOTH_SERVICE(ota, "/.u/desired", ota_downlink, NULL);
