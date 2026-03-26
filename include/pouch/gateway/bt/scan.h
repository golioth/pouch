/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * Start Bluetooth scanning for devices.
 *
 * Start active Bluetooth scanning for devices that expose Pouch Service UUID (0xFC49 or
 * 89a316ae-89b7-4ef6-b1d3-5c9a6e27d272 for backward compatibility) with vendor data indicating:
 * - compatible 'version'
 * - sync request set in 'flags'
 */
void pouch_gateway_scan_start(void);
