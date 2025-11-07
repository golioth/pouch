/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pouch/transport/gatt/common/packetizer.h>

/**
 * Callback executed to send acknowledgements to the sender
 *
 * @param arg User arg, supplied when receiver was created
 * @param data The bytes to send
 * length length The length of @p data
 *
 * @return 0 on success, or a negative error code otherwise
 */
typedef int (*pouch_gatt_send_ack_fn)(void *arg, const void *data, size_t length);

/**
 * Callback executed to pass received data to the application
 *
 * @param arg User arg, supplied when receiver was created
 * @param data The received data
 * @param length The length of @p data
 * @param is_first True if this is the first packet in a transfer
 * @param is_last True if this is the last packet in a transfer
 *
 * @note It is possible for both @p is_first and @p is_last to
 *       be true, if the transfer is wholly contained in a single
 *       packet.
 *
 * @return 0 on success, or a negative error code otherwise
 */
typedef int (*pouch_gatt_receiver_data_push_fn)(void *arg,
                                                const void *data,
                                                size_t length,
                                                bool is_first,
                                                bool is_last);

struct pouch_gatt_receiver;

/**
 * Create a Pouch GATT Receiver
 *
 * @param send_ack Callback function used to send acknowledgements to the sender
 * @param send_ack_arg User arg passed to @p send_ack
 * @param push Callback function used to pass received data to the application
 * @param push_arg User arg passed to @p push
 * @param window The size of the sliding window to use
 *
 * @return A pointer to the created Pouch GATT receiver, or NULL on error
 */
struct pouch_gatt_receiver *pouch_gatt_receiver_create(pouch_gatt_send_ack_fn send_ack,
                                                       void *send_ack_arg,
                                                       pouch_gatt_receiver_data_push_fn push,
                                                       void *push_arg,
                                                       uint8_t window);

/**
 * Destroy a Pouch GATT Receiver
 *
 * @param receiver The receiver to destroy
 */
void pouch_gatt_receiver_destroy(struct pouch_gatt_receiver *receiver);

/**
 * Push data to a receiver
 *
 * @param receiver The receiver for this transfer
 * @param data The data packet
 * @param length The length of @p data
 *
 * @return 0 on success, or a negative error code otherwise
 */
int pouch_gatt_receiver_receive_data(struct pouch_gatt_receiver *receiver,
                                     const void *data,
                                     size_t length,
                                     bool *complete);

/**
 * Send a NACK to the sender.
 *
 * @note Note that this function takes a send_ack callback and not a receiver
 *       pointer. This is so that NACKs can be sent when there is no active
 *       transfer, which is required when a data packet is received while idle.
 *
 * @param send_ack Callback function used to send acks to the sender
 * @param send_ack_arg User arg supplied to @send_ack
 * @param code The reason for the NACK
 */
int pouch_gatt_receiver_send_nack(pouch_gatt_send_ack_fn send_ack,
                                  void *send_ack_arg,
                                  enum pouch_gatt_ack_code code);