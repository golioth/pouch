/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "packet.h"
#include <errno.h>

#define POUCH_SAR_TX_PKT_FLAG_MASK \
    (POUCH_SAR_TX_PKT_FLAG_FIRST | POUCH_SAR_TX_PKT_FLAG_LAST | POUCH_SAR_TX_PKT_FLAG_FIN)

int pouch_sar_tx_pkt_decode(const void *buf, size_t buf_len, struct pouch_sar_tx_pkt *pkt)
{
    if (buf_len < POUCH_SAR_TX_PKT_HEADER_LEN)
    {
        return -EINVAL;
    }

    const uint8_t *bytes = buf;
    pkt->flags = bytes[0] & POUCH_SAR_TX_PKT_FLAG_MASK;  // ignoring any undocumented flags
    if (pkt->flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        if (pkt->flags != POUCH_SAR_TX_PKT_FLAG_FIN || buf_len != POUCH_SAR_TX_PKT_HEADER_LEN)
        {
            return -EINVAL;
        }

        pkt->seq = 0;
        pkt->len = 0;
        pkt->data = NULL;

        // the ack code is placed in the seq byte:
        if (bytes[1] != POUCH_RECEIVER_CODE_ACK)
        {
            // Using the flags field to communicate idle state.
            // Note that this is different from the encoded data format.
            pkt->flags |= POUCH_SAR_TX_PKT_FLAG_IDLE;
        }

        return 0;
    }

    pkt->seq = bytes[1];
    pkt->data = &bytes[2];
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
    pkt->code = bytes[0];
    pkt->seq = bytes[1];
    pkt->window = bytes[2];

    return 0;
}

int pouch_sar_tx_pkt_encode(const struct pouch_sar_tx_pkt *pkt, void *dst, size_t *len)
{
    if (pkt == NULL || dst == NULL || len == NULL)
    {
        return -EINVAL;
    }

    uint8_t *bytes = dst;
    bytes[0] = pkt->flags & POUCH_SAR_TX_PKT_FLAG_MASK;
    if (pkt->flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        if (*len < POUCH_SAR_TX_PKT_HEADER_LEN)
        {
            return -EINVAL;
        }

        bytes[1] = pkt->flags & POUCH_SAR_TX_PKT_FLAG_IDLE ? POUCH_RECEIVER_CODE_NACK_IDLE
                                                           : POUCH_RECEIVER_CODE_ACK;
        *len = POUCH_SAR_TX_PKT_HEADER_LEN;
        return 0;
    }

    if (pkt->data == NULL)
    {
        return -EINVAL;
    }

    bytes[1] = pkt->seq;
    if (pkt->data != &bytes[2])
    {
        memcpy(&bytes[2], pkt->data, pkt->len);
    }

    *len = POUCH_SAR_TX_PKT_HEADER_LEN + pkt->len;

    return 0;
}

void pouch_sar_rx_pkt_encode(const struct pouch_sar_rx_pkt *pkt, uint8_t dst[POUCH_SAR_RX_PKT_LEN])
{
    dst[0] = pkt->code;
    dst[1] = pkt->seq;
    dst[2] = pkt->window;
}
