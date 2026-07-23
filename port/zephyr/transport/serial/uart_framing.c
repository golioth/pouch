/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "uart_framing.h"

#include <string.h>

size_t pouch_uart_frame_encode(uint8_t *out, size_t out_size, const uint8_t *frame, size_t len)
{
    if (out == NULL || frame == NULL || len == 0 || len > UINT16_MAX)
    {
        return 0;
    }

    if (out_size < len + POUCH_UART_FRAME_OVERHEAD)
    {
        return 0;
    }

    out[0] = POUCH_UART_FRAME_SOF;
    out[1] = (uint8_t) (len >> 8);
    out[2] = (uint8_t) (len & 0xff);
    memcpy(&out[POUCH_UART_FRAME_OVERHEAD], frame, len);

    return len + POUCH_UART_FRAME_OVERHEAD;
}

void pouch_uart_framer_init(struct pouch_uart_framer *framer, uint8_t *buf, size_t buf_size)
{
    framer->state = POUCH_UART_FRAMER_SOF;
    framer->buf = buf;
    framer->buf_size = buf_size;
    framer->expected = 0;
    framer->got = 0;
}

size_t pouch_uart_framer_feed(struct pouch_uart_framer *framer, uint8_t byte)
{
    switch (framer->state)
    {
        case POUCH_UART_FRAMER_SOF:
            if (byte == POUCH_UART_FRAME_SOF)
            {
                framer->state = POUCH_UART_FRAMER_LEN_HI;
            }
            break;

        case POUCH_UART_FRAMER_LEN_HI:
            framer->expected = (size_t) byte << 8;
            framer->state = POUCH_UART_FRAMER_LEN_LO;
            break;

        case POUCH_UART_FRAMER_LEN_LO:
            framer->expected |= byte;
            framer->got = 0;

            /* Reject zero-length and oversized frames, resynchronizing on the
             * next SOF. */
            if (framer->expected == 0 || framer->expected > framer->buf_size)
            {
                framer->state = POUCH_UART_FRAMER_SOF;
            }
            else
            {
                framer->state = POUCH_UART_FRAMER_PAYLOAD;
            }
            break;

        case POUCH_UART_FRAMER_PAYLOAD:
            framer->buf[framer->got++] = byte;
            if (framer->got == framer->expected)
            {
                framer->state = POUCH_UART_FRAMER_SOF;
                return framer->expected;
            }
            break;
    }

    return 0;
}
