/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "packet.h"

#include <errno.h>

/* Header byte bit positions */
#define HDR_DATA (1 << 7)
#define HDR_ERR (1 << 6)
#define HDR_FIRST (1 << 5)
#define HDR_LAST (1 << 4)
#define HDR_CH_MASK 0x0f

/* Reserved bits in an ACK frame must be zero. */
#define HDR_ACK_RESERVED_MASK (HDR_FIRST | HDR_LAST)

uint8_t pouch_serial_header_encode(const struct pouch_serial_header *hdr)
{
    uint8_t byte = hdr->channel & HDR_CH_MASK;

    if (hdr->is_data)
    {
        byte |= HDR_DATA;
        if (hdr->first)
        {
            byte |= HDR_FIRST;
        }
        if (hdr->last)
        {
            byte |= HDR_LAST;
        }
    }

    if (hdr->err)
    {
        byte |= HDR_ERR;
    }

    return byte;
}

int pouch_serial_header_decode(uint8_t byte, struct pouch_serial_header *hdr)
{
    hdr->is_data = !!(byte & HDR_DATA);
    hdr->err = !!(byte & HDR_ERR);
    hdr->channel = byte & HDR_CH_MASK;

    if (hdr->is_data)
    {
        hdr->first = !!(byte & HDR_FIRST);
        hdr->last = !!(byte & HDR_LAST);

        /* ERR must always be accompanied by LAST on data frames. */
        if (hdr->err && !hdr->last)
        {
            hdr->last = true;
        }
    }
    else
    {
        hdr->first = false;
        hdr->last = false;

        /* Reserved bits must be zero in ACK frames. */
        if (byte & HDR_ACK_RESERVED_MASK)
        {
            return -EINVAL;
        }
    }

    return 0;
}
