/*
 * Copyright (c) 2024 Golioth
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "packetizer.h"

#define TF_PACKET_START 0x01
#define TF_PACKET_MORE 0x02
#define TF_PACKET_END 0x03

struct tf_packet
{
    uint8_t ctrl;
    uint8_t data[];
} __packed;

enum tf_packetizer_fill_type
{
    TF_PACKETIZER_FILL_BUFFER,
    TF_PACKETIZER_FILL_CALLBACK,
};

struct tf_packetizer
{
    enum tf_packetizer_fill_type type;
    union
    {
        struct
        {
            const void *src;
            size_t len;

        } buf;
        struct
        {
            tf_packetizer_fill_cb func;
            void *user_arg;
        } cb;
    };
    bool is_first;
    int error;
};

struct tf_packetizer *tf_packetizer_start_buffer(const void *src, size_t src_len)
{
    struct tf_packetizer *packetizer = malloc(sizeof(struct tf_packetizer));
    if (NULL != packetizer)
    {
        packetizer->type = TF_PACKETIZER_FILL_BUFFER;
        packetizer->buf.src = src;
        packetizer->buf.len = src_len;
        packetizer->is_first = true;
        packetizer->error = 0;
    }

    return packetizer;
}

struct tf_packetizer *tf_packetizer_start_callback(tf_packetizer_fill_cb func, void *user_arg)
{
    struct tf_packetizer *packetizer = malloc(sizeof(struct tf_packetizer));
    if (NULL != packetizer)
    {
        packetizer->type = TF_PACKETIZER_FILL_CALLBACK;
        packetizer->cb.func = func;
        packetizer->cb.user_arg = user_arg;
        packetizer->is_first = true;
        packetizer->error = 0;
    }

    return packetizer;
}

enum tf_packetizer_result tf_packetizer_get(struct tf_packetizer *packetizer,
                                            void *dst,
                                            size_t *dst_len)
{
    struct tf_packet *pkt = dst;
    enum tf_packetizer_result ret = TF_PACKETIZER_MORE_DATA;

    if (*dst_len <= sizeof(struct tf_packet))
    {
        packetizer->error = ENOMEM;
        ret = TF_PACKETIZER_ERROR;
        goto finish;
    }

    if (packetizer->is_first)
    {
        pkt->ctrl = TF_PACKET_START;
        packetizer->is_first = false;
    }
    else
    {
        pkt->ctrl = TF_PACKET_MORE;
    }

    if (TF_PACKETIZER_FILL_BUFFER == packetizer->type)
    {
        size_t bytes_to_copy = *dst_len - sizeof(struct tf_packet);
        if (bytes_to_copy >= packetizer->buf.len)
        {
            bytes_to_copy = packetizer->buf.len;
            pkt->ctrl = TF_PACKET_END;
            ret = TF_PACKETIZER_NO_MORE_DATA;
        }

        memcpy(pkt->data, packetizer->buf.src, bytes_to_copy);

        *dst_len = sizeof(struct tf_packet) + bytes_to_copy;
        packetizer->buf.src = (void *) ((intptr_t) packetizer->buf.src + bytes_to_copy);
        packetizer->buf.len -= bytes_to_copy;
    }

    if (TF_PACKETIZER_FILL_CALLBACK == packetizer->type)
    {
        size_t bytes_to_fill = *dst_len - sizeof(struct tf_packet);
        enum tf_packetizer_result cb_result =
            packetizer->cb.func(pkt->data, &bytes_to_fill, packetizer->cb.user_arg);

        if (TF_PACKETIZER_ERROR == cb_result)
        {
            packetizer->error = ENODATA;
            *dst_len = 0;
            ret = TF_PACKETIZER_ERROR;
            goto finish;
        }

        *dst_len = sizeof(struct tf_packet) + bytes_to_fill;

        if (TF_PACKETIZER_NO_MORE_DATA == cb_result)
        {
            pkt->ctrl = TF_PACKET_END;
            ret = TF_PACKETIZER_NO_MORE_DATA;
        }
    }

finish:
    return ret;
}

int tf_packetizer_error(struct tf_packetizer *packetizer)
{
    return packetizer->error;
}

void tf_packetizer_finish(struct tf_packetizer *packetizer)
{
    free(packetizer);
}
