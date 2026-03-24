/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <pouch/types.h>
#include <pouch/transport/certificate.h>
#include "endpoints.h"

static struct pouch_cert cert;
static size_t offset;

static int start(void)
{
    offset = 0;
    return pouch_device_certificate_get(&cert);
}

static enum pouch_result send(void *dst, size_t *dst_len)
{
    enum pouch_result res = POUCH_MORE_DATA;
    if (offset + *dst_len >= cert.size)
    {
        res = POUCH_NO_MORE_DATA;
        *dst_len = cert.size - offset;
    }

    memcpy(dst, &cert.buffer[offset], *dst_len);
    offset += *dst_len;

    return res;
}

const struct pouch_endpoint pouch_endpoint_device_cert = {
    .start = start,
    .send = send,
};
