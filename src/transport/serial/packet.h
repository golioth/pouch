/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * Decoded representation of a 1-byte Pouch Serial frame header.
 *
 * Bit layout:
 *   ACK frame  (bit 7 = 0): [ 0 | ERR | 0 | LAST | channel[3:0] ]
 *   Data frame (bit 7 = 1): [ 1 | ERR | FIRST | LAST | channel[3:0] ]
 *
 * ACK frames come in three kinds:
 *   ACK   (ERR = 0, LAST = 0): The previous frame was received, send the next one.
 *   NACK  (ERR = 1, LAST = 0): The previous frame was lost or broken, send it again.
 *   Abort (ERR = 1, LAST = 1): The receiver has abandoned the transfer.
 */
struct pouch_serial_header
{
    /** True for Data frames, false for ACK frames. */
    bool is_data;
    /**
     * Error flag. For Data frames, the transfer has been abandoned by the sender; must be sent
     * with LAST set. For ACK frames, the previous frame must be sent again (NACK), or with LAST
     * set, the transfer has been abandoned by the receiver.
     */
    bool err;
    /** First fragment of a transfer. Data frames only. */
    bool first;
    /** Last fragment of a transfer. For ACK frames, only valid with ERR, to abort the transfer. */
    bool last;
    /** Logical channel identifier (4 bits). */
    uint8_t channel;
};

/**
 * Encode a header into its 1-byte wire representation.
 *
 * @param hdr Header to encode.
 * @return Encoded byte.
 */
uint8_t pouch_serial_header_encode(const struct pouch_serial_header *hdr);

/**
 * Decode a 1-byte wire header.
 *
 * @param byte   Wire byte to decode.
 * @param hdr    Output header.
 * @return 0 on success, -EINVAL if the byte is malformed.
 */
int pouch_serial_header_decode(uint8_t byte, struct pouch_serial_header *hdr);
