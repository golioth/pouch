/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

struct ble_gap_event;

/**
 * Handle a GAP event from the NimBLE host.
 *
 * The application's GAP event handler should call this function to delegate
 * connection, subscription, and MTU events to the Pouch GATT transport.
 *
 * @param event GAP event from NimBLE.
 * @param arg   Callback argument passed by NimBLE.
 * @return 0 on success, NimBLE error code on failure.
 */
int pouch_gatt_gap_event(struct ble_gap_event *event, void *arg);

/**
 * Initialize the Pouch GATT service and register it with NimBLE.
 *
 * Must be called after nimble_port_init() and before the NimBLE host task
 * is started.
 *
 * @return 0 on success, negative errno on failure.
 */
int pouch_gatt_init(void);
