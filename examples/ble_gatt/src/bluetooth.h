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
void bluetooth_button_handler(void);

/**
 * Enable or disable the gateway request flag in the Bluetooth advertisements.
 */
void bluetooth_request_gateway(bool request);

/**
 * Initialize application Bluetooth module.
 */
int bluetooth_init(void);

/**
 * Start Bluetooth advertising.
 */
int bluetooth_start(void);
