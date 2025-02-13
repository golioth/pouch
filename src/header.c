/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "header.h"
#include "crypto.h"
#include "buf.h"
#include "cddl/header_encode.h"

#include <string.h>
#include <stdio.h>

#include <zcbor_encode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>

#define POUCH_HEADER_VERSION 0

#if defined(CONFIG_POUCH_ENCRYPTION_NONE)
#define POUCH_HEADER_MAX_LEN 5 + sizeof(CONFIG_POUCH_DEVICE_ID)
#else
#error "Unsupported encryption type"
#endif

static int write_header(struct pouch_buf *buf, size_t maxlen)
{
    struct pouch_header header = {
        .version = POUCH_HEADER_VERSION,
    };

    int err = crypto_header_get(&header.encryption_info_m);
    if (err)
    {
        return err;
    }

    return cbor_encode_pouch_header(buf_next(buf), maxlen, &header, &buf->bytes);
}

struct pouch_buf *pouch_header_create(void)
{
    // Not blocking this call:
    struct pouch_buf *header = buf_alloc(POUCH_HEADER_MAX_LEN);
    if (!header)
    {
        return NULL;
    }

    int err = write_header(header, POUCH_HEADER_MAX_LEN);
    if (err)
    {
        buf_free(header);
        return NULL;
    }

    return header;
}
