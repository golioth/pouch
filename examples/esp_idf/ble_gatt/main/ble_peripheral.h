/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

/**
 * Initialize the BLE stack (NimBLE host, GATT services, security).
 *
 * @return 0 on success, negative on failure.
 */
int ble_peripheral_init(void);

/**
 * Start BLE advertising.
 *
 * @return 0 on success, negative on failure.
 */
int ble_peripheral_start(void);

/**
 * Update the sync request flag in advertising data.
 *
 * @param request true to set the sync request flag, false to clear it.
 */
void ble_peripheral_request_gateway(bool request);
