/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "packet.h"
#include <errno.h>

#define TX_PKT_OFFSET_FLAGS 0
#define TX_PKT_OFFSET_SEQ 1
#define TX_PKT_OFFSET_FIN_CODE 1
#define TX_PKT_OFFSET_DATA 2

#define RX_PKT_OFFSET_CODE 0
#define RX_PKT_OFFSET_SEQ 1
#define RX_PKT_OFFSET_WINDOW 2


#define POUCH_SAR_TX_PKT_FLAG_MASK \
    (POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST | POUCH_SAR_TX_PKT_FLAG_FIN)

int pouch_sar_tx_pkt_decode(const void *buf, size_t buf_len, struct pouch_sar_tx_pkt *pkt)
{
    if (buf_len < POUCH_SAR_TX_PKT_HEADER_LEN)
    {
        return -EINVAL;
    }

    const uint8_t *bytes = buf;
    pkt->flags =
        bytes[TX_PKT_OFFSET_FLAGS] & POUCH_SAR_TX_PKT_FLAG_MASK;  // ignoring any undocumented flags
    if (pkt->flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        if (pkt->flags != POUCH_SAR_TX_PKT_FLAG_FIN || buf_len != POUCH_SAR_TX_PKT_HEADER_LEN)
        {
            return -EINVAL;
        }

        if (buf_len != POUCH_SAR_TX_PKT_HEADER_LEN)
        {
            return -EINVAL;
        }

        pkt->seq = 0;
        pkt->len = 0;
        pkt->data = NULL;

        if (bytes[TX_PKT_OFFSET_FIN_CODE] == POUCH_RECEIVER_CODE_ACK)
        {
            // Using the flags field to communicate idle state.
            // Note that this is different from the encoded data format.
            pkt->flags |= POUCH_SAR_TX_PKT_FLAG_IDLE;
        }

        return 0;
    }

    pkt->seq = bytes[TX_PKT_OFFSET_SEQ];
    if (buf_len == POUCH_SAR_TX_PKT_HEADER_LEN)
    {
        pkt->data = NULL;
        pkt->len = 0;
        return 0;
    }

    pkt->data = &bytes[POUCH_SAR_TX_PKT_HEADER_LEN];
    pkt->len = buf_len - POUCH_SAR_TX_PKT_HEADER_LEN;
    return 0;
}

int pouch_sar_rx_pkt_decode(const void *buf, size_t buf_len, struct pouch_sar_rx_pkt *pkt)
{
    if (buf_len != POUCH_SAR_RX_PKT_LEN)
    {
        return -EINVAL;
    }

    const uint8_t *bytes = buf;
    pkt->code = bytes[RX_PKT_OFFSET_CODE];
    pkt->seq = bytes[RX_PKT_OFFSET_SEQ];
    pkt->window = bytes[RX_PKT_OFFSET_WINDOW];

    return 0;
}

int pouch_sar_tx_pkt_encode(const struct pouch_sar_tx_pkt *pkt, void *dst, size_t *len)
{
    if (pkt == NULL || dst == NULL || len == NULL)
    {
        return -EINVAL;
    }

    uint8_t *bytes = dst;
    bytes[TX_PKT_OFFSET_FLAGS] = pkt->flags & POUCH_SAR_TX_PKT_FLAG_MASK;
    if (pkt->flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        if (*len < POUCH_SAR_TX_PKT_HEADER_LEN)
        {
            return -EINVAL;
        }

        bytes[TX_PKT_OFFSET_FIN_CODE] = (pkt->flags & POUCH_SAR_TX_PKT_FLAG_IDLE)
            ? POUCH_RECEIVER_CODE_NACK_IDLE
            : POUCH_RECEIVER_CODE_ACK;

        *len = POUCH_SAR_TX_PKT_HEADER_LEN;
        return 0;
    }

    if (pkt->data == NULL)
    {
        return -EINVAL;
    }

    bytes[TX_PKT_OFFSET_SEQ] = pkt->seq;
    if (pkt->data != &bytes[TX_PKT_OFFSET_DATA])
    {
        memcpy(&bytes[TX_PKT_OFFSET_DATA], pkt->data, pkt->len);
    }

    *len = TX_PKT_OFFSET_DATA + pkt->len;

    return 0;
}

void pouch_sar_rx_pkt_encode(const struct pouch_sar_rx_pkt *pkt, uint8_t dst[POUCH_SAR_RX_PKT_LEN])
{
    dst[RX_PKT_OFFSET_CODE] = pkt->code;
    dst[RX_PKT_OFFSET_SEQ] = pkt->seq;
    dst[RX_PKT_OFFSET_WINDOW] = pkt->window;
}
