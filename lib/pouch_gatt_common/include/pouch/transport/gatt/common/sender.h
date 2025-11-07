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
 * Callback executed to send data to the receiver
 *
 * @param arg User arg, supplied when the sender was created
 * @param data The data to send
 * @param length The length of @p data
 *
 * @return 0 on success, or a negative error code otherwise
 */
typedef int (*pouch_gatt_send_fn)(void *arg, const void *data, size_t length);

struct pouch_gatt_sender;

/**
 * Create a Pouch GATT Sender
 *
 * @param packetizer Initialized packetizer from which to retrieve packets
 * @param send Callback function used to send data to the receiver
 * @param send_arg User arg supplied to @p send
 * @param mtu The Maximum Transmission Unit for the transport link
 *
 * @return A pointer to the created Pouch GATT sender, or NULL on error
 */
struct pouch_gatt_sender *pouch_gatt_sender_create(struct pouch_gatt_packetizer *packetizer,
                                                   pouch_gatt_send_fn send,
                                                   void *send_arg,
                                                   size_t mtu);

/**
 * Destroy a Pouch GATT Sender
 *
 * @param sender The sender to destroy
 */
void pouch_gatt_sender_destroy(struct pouch_gatt_sender *sender);

/**
 * Receive an acknowledgement from the receiver
 *
 * @param sender The sender for this transfer
 * @param data The acknowledgment payload
 * @param length The length of @p data
 * @param[out] complete Set to true if the transfer is complete
 *
 * @return 0 on successfully receiving an ACK
 * @return A positive enum pouch_gatt_ack_code on NACK
 * @return A negative error code otherwise
 */
int pouch_gatt_sender_receive_ack(struct pouch_gatt_sender *sender,
                                  const void *data,
                                  size_t length,
                                  bool *complete);

/**
 * Notify the sender that new data is available to send.
 *
 * The sender will autonomously retrieve data from the packetizer to send
 * to the receiver until the packetizer indicates it has no data to send,
 * but the transfer is not complete (e.g. if the packetizer is awaiting
 * data from a cloud service). This function can be called to notify the
 * sender that more data has been made available to the packetizer.
 *
 * @param sender The sender for this transfer
 *
 * @return 0 on succcess, or a negative error code otherwise
 */
int pouch_gatt_sender_data_available(struct pouch_gatt_sender *sender);

/**
 * Send a FIN packet to the receiver.
 *
 * @note Note that this function takes a send callback and not a sender
 *       pointer. This is so that FIN packets can be sent when there is
 *       no active transfer, which is required when an acknowledgment is
 *       received while idle.
 *
 * @param send Callback function used to send data to the receiver
 * @param send_arg User arg supplied to @send
 * @param code The reason for the FIN
 */
int pouch_gatt_sender_send_fin(pouch_gatt_send_fn send,
                               void *send_arg,
                               enum pouch_gatt_ack_code code);
