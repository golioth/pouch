/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "types.h"
#include <stdbool.h>

int gateway_bt_discover(struct broker_bt_gatt_device *device,
                        broker_bt_gatt_discover_callback_t on_complete);
