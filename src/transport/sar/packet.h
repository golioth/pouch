/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POUCH_SAR_TX_PKT_HEADER_LEN 2
#define POUCH_SAR_RX_PKT_LEN 3
#define POUCH_SAR_SEQ_MAX 0xff
#define POUCH_SAR_SEQ_MASK POUCH_SAR_SEQ_MAX
#define POUCH_SAR_WINDOW_MAX 127

enum pouch_sar_rx_pkt_code
{
    POUCH_RECEIVER_CODE_ACK,
    POUCH_RECEIVER_CODE_NACK_UNKNOWN,
    POUCH_RECEIVER_CODE_NACK_IDLE,
};

enum pouch_sar_tx_pkt_flag
{
    POUCH_SAR_TX_PKT_FLAG_FIRST = (1 << 0),
    POUCH_SAR_TX_PKT_FLAG_LAST = (1 << 1),
    POUCH_SAR_TX_PKT_FLAG_FIN = (1 << 2),
    POUCH_SAR_TX_PKT_FLAG_IDLE = (1 << 3),
};

struct pouch_sar_tx_pkt
{
    uint8_t seq;
    enum pouch_sar_tx_pkt_flag flags;
    size_t len;
    const uint8_t *data;
};

struct pouch_sar_rx_pkt
{
    enum pouch_sar_rx_pkt_code code;
    uint8_t seq;
    uint8_t window;
};

int pouch_sar_tx_pkt_decode(const void *buf, size_t buf_len, struct pouch_sar_tx_pkt *pkt);
int pouch_sar_tx_pkt_encode(const struct pouch_sar_tx_pkt *pkt, void *dst, size_t *len);

int pouch_sar_rx_pkt_decode(const void *buf, size_t buf_len, struct pouch_sar_rx_pkt *pkt);
void pouch_sar_rx_pkt_encode(const struct pouch_sar_rx_pkt *pkt, uint8_t dst[POUCH_SAR_RX_PKT_LEN]);
