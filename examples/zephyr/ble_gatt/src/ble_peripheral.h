/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Button handler for Bluetooth OOB authentication.
 */
void ble_peripheral_button_handler(void);

/**
 * Enable or disable the gateway request flag in the Bluetooth advertisements.
 */
void ble_peripheral_request_gateway(bool request);

/**
 * Initialize application Bluetooth module.
 */
int ble_peripheral_init(void);

/**
 * Start Bluetooth advertising.
 */
int ble_peripheral_start(void);
