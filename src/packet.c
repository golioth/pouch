/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "encrypt.h"
#include "packet.h"

#include <string.h>
#include <stdio.h>

#include <zcbor_encode.h>
#include <zephyr/kernel.h>

#define POUCH_HEADER_VERSION 0

ssize_t entry_header_write(uint8_t *buf,
                           size_t maxlen,
                           const char *path,
                           enum pouch_content_type content_type,
                           size_t data_len)
{
    size_t path_len = strlen(path);
    if (path_len > POUCH_ENTRY_PATH_MAX_LEN || content_type > UINT8_MAX)
    {
        return -EINVAL;
    }

    bool ok;
    ZCBOR_STATE_E(zse, 1, buf, maxlen, 1);

    ok = zcbor_list_start_encode(zse, 3);
    if (!ok)
    {
        return -EMSGSIZE;
    };

    ok = zcbor_tstr_encode_ptr(zse, path, path_len);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    ok = zcbor_uint32_put(zse, content_type);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    ok = zcbor_uint32_put(zse, data_len);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    ok = zcbor_list_end_encode(zse, 3);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    return (intptr_t) zse->payload - (intptr_t) buf;
}

ssize_t pouch_header_write(uint8_t *buf, size_t maxlen)
{
    bool ok;
    ZCBOR_STATE_E(zse, 1, buf, maxlen, 1);

    ok = zcbor_list_start_encode(zse, 9);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    ok = zcbor_uint32_put(zse, POUCH_HEADER_VERSION);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    ok = encrypt_pouch_header_encryption_info_encode(zse);
    if (!ok)
    {
        return -EMSGSIZE;
    }

    ok = zcbor_list_end_encode(zse, 9);
    if (!ok)
    {
        return -EIO;
    }

    return zse->payload - buf;
}
