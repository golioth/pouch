/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <pouch/transport/gatt/common/packetizer.h>

typedef int (*pouch_gatt_send_fn)(void *arg, const void *data, size_t len);

struct pouch_gatt_sender;

int pouch_gatt_sender_receive_ack(struct pouch_gatt_sender *sender,
                                  const void *data,
                                  size_t length,
                                  bool *complete);

int pouch_gatt_sender_data_available(struct pouch_gatt_sender *sender);

void pouch_gatt_sender_destroy(struct pouch_gatt_sender *sender);

struct pouch_gatt_sender *pouch_gatt_sender_create(struct pouch_gatt_packetizer *packetizer,
                                                   pouch_gatt_send_fn send,
                                                   void *send_arg,
                                                   size_t mtu);