/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/toolchain.h>

#include <pouch/transport/gatt/common/packetizer.h>

#define POUCH_GATT_PACKET_FIRST (1 << 0)
#define POUCH_GATT_PACKET_LAST (1 << 1)
#define POUCH_GATT_PACKET_FIN (1 << 2)

struct pouch_gatt_packet
{
    uint8_t flags;
    union
    {
        uint8_t seq;  /* For data packets */
        uint8_t code; /* For FIN packets, enum pouch_gatt_ack_code */
    };
    uint8_t data[];
} __packed;

BUILD_ASSERT(sizeof(struct pouch_gatt_packet) == POUCH_GATT_FIN_SIZE,
             "POUCH_GATT_FIN_SIZE is wrong");

struct pouch_gatt_ack
{
    uint8_t code; /* enum pouch_gatt_ack_code */
    uint8_t seq;
    uint8_t window;
} __packed;

BUILD_ASSERT(sizeof(struct pouch_gatt_ack) == POUCH_GATT_ACK_SIZE, "POUCH_GATT_ACK_SIZE is wrong");

enum pouch_gatt_packetizer_fill_type
{
    POUCH_GATT_PACKETIZER_FILL_BUFFER,
    POUCH_GATT_PACKETIZER_FILL_CALLBACK,
};

struct pouch_gatt_packetizer
{
    enum pouch_gatt_packetizer_fill_type type;
    union
    {
        struct
        {
            const void *src;
            size_t len;

        } buf;
        struct
        {
            pouch_gatt_packetizer_fill_cb func;
            void *user_arg;
        } cb;
    };
    uint8_t seq;
    bool is_first;
    int error;
};

struct pouch_gatt_packetizer *pouch_gatt_packetizer_start_buffer(const void *src, size_t src_len)
{
    struct pouch_gatt_packetizer *packetizer = malloc(sizeof(struct pouch_gatt_packetizer));
    if (NULL != packetizer)
    {
        packetizer->type = POUCH_GATT_PACKETIZER_FILL_BUFFER;
        packetizer->buf.src = src;
        packetizer->buf.len = src_len;
        packetizer->seq = 0;
        packetizer->is_first = true;
        packetizer->error = 0;
    }

    return packetizer;
}

struct pouch_gatt_packetizer *pouch_gatt_packetizer_start_callback(
    pouch_gatt_packetizer_fill_cb func,
    void *user_arg)
{
    struct pouch_gatt_packetizer *packetizer = malloc(sizeof(struct pouch_gatt_packetizer));
    if (NULL != packetizer)
    {
        packetizer->type = POUCH_GATT_PACKETIZER_FILL_CALLBACK;
        packetizer->cb.func = func;
        packetizer->cb.user_arg = user_arg;
        packetizer->seq = 0;
        packetizer->is_first = true;
        packetizer->error = 0;
    }

    return packetizer;
}

enum pouch_gatt_packetizer_result pouch_gatt_packetizer_get(
    struct pouch_gatt_packetizer *packetizer,
    void *dst,
    size_t *dst_len)
{
    struct pouch_gatt_packet *pkt = dst;
    pkt->flags = 0;
    enum pouch_gatt_packetizer_result ret = POUCH_GATT_PACKETIZER_MORE_DATA;

    if (*dst_len <= sizeof(struct pouch_gatt_packet))
    {
        packetizer->error = ENOMEM;
        ret = POUCH_GATT_PACKETIZER_ERROR;
        goto finish;
    }

    if (POUCH_GATT_PACKETIZER_FILL_BUFFER == packetizer->type)
    {
        size_t bytes_to_copy = *dst_len - sizeof(struct pouch_gatt_packet);
        if (bytes_to_copy >= packetizer->buf.len)
        {
            bytes_to_copy = packetizer->buf.len;
            pkt->flags |= POUCH_GATT_PACKET_LAST;
            ret = POUCH_GATT_PACKETIZER_NO_MORE_DATA;
        }

        memcpy(pkt->data, packetizer->buf.src, bytes_to_copy);

        *dst_len = sizeof(struct pouch_gatt_packet) + bytes_to_copy;
        packetizer->buf.src = (void *) ((intptr_t) packetizer->buf.src + bytes_to_copy);
        packetizer->buf.len -= bytes_to_copy;
    }

    if (POUCH_GATT_PACKETIZER_FILL_CALLBACK == packetizer->type)
    {
        size_t bytes_to_fill = *dst_len - sizeof(struct pouch_gatt_packet);
        enum pouch_gatt_packetizer_result cb_result =
            packetizer->cb.func(pkt->data, &bytes_to_fill, packetizer->cb.user_arg);

        if (POUCH_GATT_PACKETIZER_ERROR == cb_result)
        {
            packetizer->error = ENODATA;
            *dst_len = 0;
            ret = POUCH_GATT_PACKETIZER_ERROR;
            goto finish;
        }

        *dst_len = sizeof(struct pouch_gatt_packet) + bytes_to_fill;

        if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == cb_result)
        {
            pkt->flags |= POUCH_GATT_PACKET_LAST;
            ret = POUCH_GATT_PACKETIZER_NO_MORE_DATA;
        }
        else if (bytes_to_fill == 0)
        {
            ret = POUCH_GATT_PACKETIZER_EMPTY_PAYLOAD;
            goto finish;
        }
    }

    if (packetizer->is_first)
    {
        pkt->flags |= POUCH_GATT_PACKET_FIRST;
        packetizer->is_first = false;
    }

    pkt->seq = packetizer->seq++;

finish:
    return ret;
}

int pouch_gatt_packetizer_error(struct pouch_gatt_packetizer *packetizer)
{
    return packetizer->error;
}

void pouch_gatt_packetizer_finish(struct pouch_gatt_packetizer *packetizer)
{
    free(packetizer);
}

ssize_t pouch_gatt_packetizer_decode(const void *buf,
                                     size_t buf_len,
                                     const void **payload,
                                     bool *is_first,
                                     bool *is_last,
                                     unsigned int *seq)
{
    if (buf == NULL || payload == NULL || is_first == NULL || is_last == NULL)
    {
        return -EINVAL;
    }

    if (buf_len < sizeof(struct pouch_gatt_packet))
    {
        return -EINVAL;
    }

    const struct pouch_gatt_packet *pkt = buf;

    *is_first = (0 != (pkt->flags & POUCH_GATT_PACKET_FIRST));
    *is_last = (0 != (pkt->flags & POUCH_GATT_PACKET_LAST));

    *payload = &pkt->data;
    *seq = pkt->seq;

    return buf_len - sizeof(struct pouch_gatt_packet);
}

int pouch_gatt_packetizer_get_sequence(const void *packet, size_t length)
{
    const struct pouch_gatt_packet *pkt = packet;

    return pkt->seq;
}

ssize_t pouch_gatt_ack_encode(void *buf,
                              size_t buf_len,
                              enum pouch_gatt_ack_code code,
                              int seq,
                              int window)
{
    if (buf_len < sizeof(struct pouch_gatt_ack))
    {
        return -EINVAL;
    }

    struct pouch_gatt_ack *ack = buf;
    ack->code = code;
    ack->seq = seq;
    ack->window = window;

    return sizeof(struct pouch_gatt_ack);
}

int pouch_gatt_ack_decode(const void *buf,
                          size_t buf_len,
                          enum pouch_gatt_ack_code *code,
                          unsigned int *seq,
                          unsigned int *window)
{
    if (sizeof(struct pouch_gatt_ack) > buf_len)
    {
        return -EINVAL;
    }

    const struct pouch_gatt_ack *ack = buf;

    *code = ack->code;
    *seq = ack->seq;

    if (ack->code == POUCH_GATT_ACK)
    {
        *window = ack->window;
    }
    else
    {
        *window = 0;
    }

    return 0;
}

bool pouch_gatt_packetizer_is_ack(const void *data, size_t length)
{
    unsigned int last_acknowledged;
    unsigned int offered_window;
    enum pouch_gatt_ack_code code;
    int err = pouch_gatt_ack_decode(data, length, &code, &last_acknowledged, &offered_window);

    return !err && POUCH_GATT_ACK == code;
}

ssize_t pouch_gatt_packetizer_fin_encode(void *buf, size_t buf_len, enum pouch_gatt_ack_code code)
{
    if (buf_len < sizeof(struct pouch_gatt_packet))
    {
        return -EINVAL;
    }

    struct pouch_gatt_packet *pkt = buf;
    pkt->flags = POUCH_GATT_PACKET_FIN;
    pkt->code = code;

    return sizeof(struct pouch_gatt_packet);
}

bool pouch_gatt_packetizer_is_fin(const void *buf, size_t buf_len, enum pouch_gatt_ack_code *code)
{
    if (buf_len < sizeof(struct pouch_gatt_packet))
    {
        return false;
    }

    const struct pouch_gatt_packet *pkt = buf;

    if (0 != (pkt->flags & POUCH_GATT_PACKET_FIN))
    {
        *code = pkt->code;
        return true;
    }

    return false;
}
