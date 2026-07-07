/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @file
 * @brief Pouch UART broker transport (ESP-IDF)
 *
 * Start the Pouch exchange sequence with the device connected over UART.
 */

/**
 * Begin the broker exchange sequence over the configured UART.
 *
 * Drives the full Info -> Server Cert -> Device Cert -> Uplink ->
 * Downlink exchange. The adapter's @c end callback is invoked when the
 * sequence completes or fails.
 *
 * Must be called after the pouch UART broker transport has been initialized
 * at application startup.
 */
void pouch_uart_broker_start(void);
