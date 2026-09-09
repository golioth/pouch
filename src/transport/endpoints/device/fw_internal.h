/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

/** Write a 32-bit value to the wire, little endian. */
static inline void pouch_serial_put_le32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t) (v & 0xff);
    dst[1] = (uint8_t) ((v >> 8) & 0xff);
    dst[2] = (uint8_t) ((v >> 16) & 0xff);
    dst[3] = (uint8_t) ((v >> 24) & 0xff);
}

/** Write a 16-bit value to the wire, little endian. */
static inline void pouch_serial_put_le16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t) (v & 0xff);
    dst[1] = (uint8_t) ((v >> 8) & 0xff);
}

/**
 * Discard any verdict held from an earlier image.
 *
 * Called when a new image is announced, so that pouch_serial_fw_status_get()
 * keeps meaning "the verdict for the image announced most recently" whichever
 * mechanism announced it.
 */
void pouch_serial_fw_status_reset(void);
