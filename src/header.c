/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "header.h"
#include "cert.h"
#include "crypto.h"
#include "buf.h"
#include "cddl/header_encode.h"
#include "saead/session.h"

#include <string.h>
#include <stdio.h>

#include <zcbor_encode.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>

#define POUCH_HEADER_VERSION 1

// CBOR array start + version
#define POUCH_HEADER_OVERHEAD 2

#if defined(CONFIG_POUCH_ENCRYPTION_NONE)

/* CBOR encryption_type + 32 byte string declaration. Assumes that the device ID max length is less
 * than 256 bytes.
 */
#define POUCH_HEADER_OVERHEAD_ENCRYPTION_NONE 3

#define POUCH_HEADER_MAX_LEN \
    (POUCH_HEADER_OVERHEAD + POUCH_HEADER_OVERHEAD_ENCRYPTION_NONE + POUCH_DEVICE_ID_MAX_LEN)
#elif defined(CONFIG_POUCH_ENCRYPTION_SAEAD)
#define POUCH_HEADER_MAX_LEN (16 + SESSION_ID_LEN + CERT_REF_SHORT_LEN)
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

    size_t len = 0;
    err = cbor_encode_pouch_header(buf_next(buf), maxlen, &header, &len);
    if (err)
    {
        return err;
    }

    buf_claim(buf, len);

    return 0;
}

struct pouch_buf *pouch_header_create(void)
{
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
