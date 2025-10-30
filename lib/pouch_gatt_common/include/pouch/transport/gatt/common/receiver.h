/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef int (*pouch_gatt_send_ack_fn)(void *arg, const void *data, size_t length);
typedef int (*pouch_gatt_receiver_data_push_fn)(void *arg,
                                                const void *data,
                                                size_t length,
                                                bool is_first,
                                                bool is_last);

struct pouch_gatt_receiver;

int pouch_gatt_receiver_receive_data(struct pouch_gatt_receiver *receiver,
                                     const void *data,
                                     size_t length);

void pouch_gatt_receiver_destroy(struct pouch_gatt_receiver *receiver);

struct pouch_gatt_receiver *pouch_gatt_receiver_create(pouch_gatt_send_ack_fn send_ack,
                                                       void *send_ack_arg,
                                                       pouch_gatt_receiver_data_push_fn push,
                                                       void *push_arg,
                                                       uint8_t window);