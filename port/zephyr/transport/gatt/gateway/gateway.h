/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "types.h"

struct gatt_device *gateway_gatt_device(struct bt_conn *conn);
