/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/kernel.h>

/**
 * @brief Enable Bluetooth bonding for a specified duration
 *
 * Enables the gateway to accept Bluetooth bonding (pairing) requests from BLE devices.
 * A timeout can be specified to automatically disable bonding after a period, limiting
 * the window during which new devices can pair with the gateway.
 *
 * If bonding is already enabled, this function updates the timeout to the new value.
 *
 * @param timeout Duration for which bonding should remain enabled. Use K_FOREVER to
 *                enable bonding indefinitely until explicitly disabled.
 */
void pouch_gateway_bonding_enable(k_timeout_t timeout);

/**
 * @brief Disable Bluetooth bonding immediately
 *
 * Disables the gateway's ability to accept Bluetooth bonding (pairing) requests.
 * Any pending bonding timeout is cancelled.
 *
 * After calling this function, the gateway will reject pairing attempts from BLE devices
 * until bonding is re-enabled via pouch_gateway_bonding_enable().
 */
void pouch_gateway_bonding_disable(void);

/**
 * @brief Check if Bluetooth bonding is currently enabled
 *
 * Queries the current bonding state of the gateway.
 *
 * @return true if bonding is currently enabled and the gateway will accept pairing requests
 * @return false if bonding is disabled and the gateway will reject pairing requests
 */
bool pouch_gateway_bonding_is_enabled(void);
