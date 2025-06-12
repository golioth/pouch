/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "header.h"
#include "crypto.h"
#include "saead/session.h"
#include "cert.h"
#include "buf.h"
#include "cddl/session_info.h"

#include <string.h>
#include <stdio.h>

#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#define POUCH_HEADER_LEN (1 + sizeof(uint32_t))
#define SESSION_INFO_MAX_LEN (22 + SESSION_ID_TAG_LEN + CERT_REF_SHORT_LEN + POUCH_HEADER_LEN)

#define HEADER_TYPE_SESSION 0
#define HEADER_TYPE_POUCH 1

struct pouch_buf *pouch_header_create(pouch_id_t pouch_id)
{
    struct pouch_buf *header;
    if (pouch_id != 0)
    {
        header = buf_alloc(POUCH_HEADER_LEN);
        *buf_claim(header, 1) = HEADER_TYPE_POUCH;
    }
    else
    {
        struct session_info info;
        int err = crypto_session_info_get(&info);
        if (err)
        {
            return NULL;
        }

        header = buf_alloc(SESSION_INFO_MAX_LEN);
        if (!header)
        {
            return NULL;
        }

        *buf_claim(header, 1) = HEADER_TYPE_SESSION;
        size_t len = 0;
        err = cbor_encode_session_info(buf_next(header), SESSION_INFO_MAX_LEN, &info, &len);
        if (err)
        {
            buf_free(header);
            return NULL;
        }

        buf_claim(header, len);
    }

    sys_put_be32(pouch_id, buf_claim(header, POUCH_HEADER_LEN));

    return header;
}
