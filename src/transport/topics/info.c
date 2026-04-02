
/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pouch/types.h>
#include <pouch/certificate.h>
#include <pouch/transport/certificate.h>
#include "sar/sender.h"
#include "sar/packet.h"
#include <stdlib.h>
#include <errno.h>
#include <cddl/info_encode.h>

#define INFO_MAX_SIZE 64

enum info_flags
{
    INFO_FLAG_PROVISIONED = (1 << 0),
};

static struct
{
    uint8_t buf[INFO_MAX_SIZE];
    size_t offset;
    size_t len;
} info;

static int build_info_data(void)
{
#if CONFIG_POUCH_ENCRYPTION_SAEAD
    uint8_t snr[CERT_SERIAL_MAXLEN];
    ssize_t snr_len = pouch_server_certificate_serial_get(snr, sizeof(snr));
    if (snr_len < 0)
    {
        return snr_len;
    }
#else
    uint8_t *snr = NULL;
    ssize_t snr_len = 0;
#endif

    // TODO: Set the Provisioned flag once we have the functionality to know whether provisioning
    // has taken place.
    enum info_flags flags = 0;

    struct pouch_gatt_info cbor = {
        .pouch_gatt_info_flags = flags,
        .pouch_gatt_info_server_cert_snr =
            {
                .value = snr,
                .len = snr_len,
            },
    };

    info.len = INFO_MAX_SIZE;
    info.offset = 0;

    return cbor_encode_pouch_gatt_info(info.buf, INFO_MAX_SIZE, &cbor, &info.len);
}

static int start(struct pouch_sender *s)
{
    return build_info_data();
}

static enum pouch_result fill(struct pouch_sender *s, void *dst, size_t *dst_len)
{
    enum pouch_result res = POUCH_MORE_DATA;
    if (info.offset + *dst_len >= info.len)
    {
        res = POUCH_NO_MORE_DATA;
        *dst_len = info.len - info.offset;
    }

    memcpy(dst, &info.buf[info.offset], *dst_len);
    info.offset += *dst_len;

    return res;
}

static const struct pouch_sender_handler_api api = {
    .start = start,
    .fill = fill,
};

struct pouch_sender pouch_topic_info = {
    .handler = &api,
};
