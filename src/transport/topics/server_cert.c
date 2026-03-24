/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <errno.h>

#include <pouch/types.h>
#include <pouch/certificate.h>
#include <pouch/transport/certificate.h>
#include "sar/receiver.h"

static struct
{
    uint8_t *buffer;
    size_t size;
} cert;

static int start(struct pouch_receiver *r)
{
    cert.buffer = malloc(CONFIG_POUCH_SERVER_CERT_MAX_LEN);
    if (cert.buffer == NULL)
    {
        return -ENOMEM;
    }
    cert.size = 0;

    return 0;
}

static int recv(struct pouch_receiver *r, const void *buf, size_t len)
{
    if (cert.size + len > CONFIG_POUCH_SERVER_CERT_MAX_LEN)
    {
        // too large
        return -EINVAL;
    }

    memcpy(&cert.buffer[cert.size], buf, len);
    cert.size += len;

    return 0;
}

static void end(struct pouch_receiver *r, bool success)
{
    if (success)
    {
        struct pouch_cert server_cert = {
            .buffer = cert.buffer,
            .size = cert.size,
        };
        pouch_server_certificate_set(&server_cert);
    }

    free(cert.buffer);
    cert.buffer = NULL;
}

static const struct pouch_receiver_handler_api api = {
    .start = start,
    .recv = recv,
    .end = end,
};

struct pouch_receiver pouch_topic_server_cert = {
    .handler = &api,
};
