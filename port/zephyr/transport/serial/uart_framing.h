/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file uart_framing.h
 * @brief Length-delimited framing for Pouch Serial frames over a byte stream.
 *
 * The Pouch Serial core produces and consumes whole frames (a 1-byte serial
 * header plus payload), but a UART is a raw byte stream with no frame
 * boundaries. This module wraps each serial frame as:
 *
 *     [ SOF=0xA5 ][ len_hi ][ len_lo ][ frame bytes ... ]
 *
 * The encoder is a pure function; the decoder is a small byte-at-a-time state
 * machine so it can be driven directly from a UART RX path. Both are free of
 * Zephyr dependencies so they can be unit tested (and mirrored by a host-side
 * broker) in isolation.
 */

#define POUCH_UART_FRAME_SOF 0xA5u

/** Bytes of framing overhead added around each serial frame (SOF + 2-byte length). */
#define POUCH_UART_FRAME_OVERHEAD 3u

/**
 * Encode a serial frame into its on-wire framed representation.
 *
 * @param out      Destination buffer.
 * @param out_size Size of @p out.
 * @param frame    Serial frame bytes to encode.
 * @param len      Number of frame bytes (must be > 0 and fit in 16 bits).
 * @return Number of bytes written to @p out (len + POUCH_UART_FRAME_OVERHEAD),
 *         or 0 if the inputs are invalid or @p out is too small.
 */
size_t pouch_uart_frame_encode(uint8_t *out, size_t out_size, const uint8_t *frame, size_t len);

/** Decoder state. */
enum pouch_uart_framer_state
{
    POUCH_UART_FRAMER_SOF,
    POUCH_UART_FRAMER_LEN_HI,
    POUCH_UART_FRAMER_LEN_LO,
    POUCH_UART_FRAMER_PAYLOAD,
};

/**
 * Streaming frame decoder.
 *
 * Assembles frames into a caller-provided buffer. A frame whose declared length
 * exceeds the buffer is dropped and the decoder resynchronizes on the next SOF.
 */
struct pouch_uart_framer
{
    enum pouch_uart_framer_state state;
    uint8_t *buf;
    size_t buf_size;
    size_t expected;
    size_t got;
};

/**
 * Initialize a decoder to assemble frames into @p buf.
 *
 * @param framer   Decoder to initialize.
 * @param buf      Buffer that completed frames are assembled into.
 * @param buf_size Size of @p buf; caps the largest decodable frame.
 */
void pouch_uart_framer_init(struct pouch_uart_framer *framer, uint8_t *buf, size_t buf_size);

/**
 * Feed one received byte into the decoder.
 *
 * @param framer Decoder.
 * @param byte   Received byte.
 * @return The length of a completed frame now available in the decoder's
 *         buffer, or 0 if no frame completed on this byte.
 */
size_t pouch_uart_framer_feed(struct pouch_uart_framer *framer, uint8_t byte);
