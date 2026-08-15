/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#define POUCH_SERIAL_HEADER_LEN 1
#define POUCH_SERIAL_CH_BROKER_TO_DEVICE(ch) ((ch) & 1)

enum pouch_serial_channel_id
{
    POUCH_SERIAL_CH_INFO,        /**< Device -> Broker: device metadata and flags */
    POUCH_SERIAL_CH_SERVER_CERT, /**< Broker -> Device: Golioth server certificate chain */
    POUCH_SERIAL_CH_DEVICE_CERT, /**< Device -> Broker: device leaf certificate */
    POUCH_SERIAL_CH_DOWNLINK,    /**< Broker -> Device: inbound pouches */
    POUCH_SERIAL_CH_UPLINK,      /**< Device -> Broker: outbound pouches */

    POUCH_SERIAL_CHANNEL_COUNT,
};
