/*
 * Copyright (c) 2024 Golioth
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/toolchain.h>

#include "packetizer.h"

#define GOLIOTH_BLE_GATT_PACKET_START 0x01
#define GOLIOTH_BLE_GATT_PACKET_MORE 0x02
#define GOLIOTH_BLE_GATT_PACKET_END 0x03

struct golioth_ble_gatt_packet
{
    uint8_t ctrl;
    uint8_t data[];
} __packed;

enum golioth_ble_gatt_packetizer_fill_type
{
    GOLIOTH_BLE_GATT_PACKETIZER_FILL_BUFFER,
    GOLIOTH_BLE_GATT_PACKETIZER_FILL_CALLBACK,
};

struct golioth_ble_gatt_packetizer
{
    enum golioth_ble_gatt_packetizer_fill_type type;
    union
    {
        struct
        {
            const void *src;
            size_t len;

        } buf;
        struct
        {
            golioth_ble_gatt_packetizer_fill_cb func;
            void *user_arg;
        } cb;
    };
    bool is_first;
    int error;
};

struct golioth_ble_gatt_packetizer *golioth_ble_gatt_packetizer_start_buffer(const void *src,
                                                                             size_t src_len)
{
    struct golioth_ble_gatt_packetizer *packetizer =
        malloc(sizeof(struct golioth_ble_gatt_packetizer));
    if (NULL != packetizer)
    {
        packetizer->type = GOLIOTH_BLE_GATT_PACKETIZER_FILL_BUFFER;
        packetizer->buf.src = src;
        packetizer->buf.len = src_len;
        packetizer->is_first = true;
        packetizer->error = 0;
    }

    return packetizer;
}

struct golioth_ble_gatt_packetizer *golioth_ble_gatt_packetizer_start_callback(
    golioth_ble_gatt_packetizer_fill_cb func,
    void *user_arg)
{
    struct golioth_ble_gatt_packetizer *packetizer =
        malloc(sizeof(struct golioth_ble_gatt_packetizer));
    if (NULL != packetizer)
    {
        packetizer->type = GOLIOTH_BLE_GATT_PACKETIZER_FILL_CALLBACK;
        packetizer->cb.func = func;
        packetizer->cb.user_arg = user_arg;
        packetizer->is_first = true;
        packetizer->error = 0;
    }

    return packetizer;
}

enum golioth_ble_gatt_packetizer_result golioth_ble_gatt_packetizer_get(
    struct golioth_ble_gatt_packetizer *packetizer,
    void *dst,
    size_t *dst_len)
{
    struct golioth_ble_gatt_packet *pkt = dst;
    enum golioth_ble_gatt_packetizer_result ret = GOLIOTH_BLE_GATT_PACKETIZER_MORE_DATA;

    if (*dst_len <= sizeof(struct golioth_ble_gatt_packet))
    {
        packetizer->error = ENOMEM;
        ret = GOLIOTH_BLE_GATT_PACKETIZER_ERROR;
        goto finish;
    }

    if (packetizer->is_first)
    {
        pkt->ctrl = GOLIOTH_BLE_GATT_PACKET_START;
        packetizer->is_first = false;
    }
    else
    {
        pkt->ctrl = GOLIOTH_BLE_GATT_PACKET_MORE;
    }

    if (GOLIOTH_BLE_GATT_PACKETIZER_FILL_BUFFER == packetizer->type)
    {
        size_t bytes_to_copy = *dst_len - sizeof(struct golioth_ble_gatt_packet);
        if (bytes_to_copy >= packetizer->buf.len)
        {
            bytes_to_copy = packetizer->buf.len;
            pkt->ctrl = GOLIOTH_BLE_GATT_PACKET_END;
            ret = GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA;
        }

        memcpy(pkt->data, packetizer->buf.src, bytes_to_copy);

        *dst_len = sizeof(struct golioth_ble_gatt_packet) + bytes_to_copy;
        packetizer->buf.src = (void *) ((intptr_t) packetizer->buf.src + bytes_to_copy);
        packetizer->buf.len -= bytes_to_copy;
    }

    if (GOLIOTH_BLE_GATT_PACKETIZER_FILL_CALLBACK == packetizer->type)
    {
        size_t bytes_to_fill = *dst_len - sizeof(struct golioth_ble_gatt_packet);
        enum golioth_ble_gatt_packetizer_result cb_result =
            packetizer->cb.func(pkt->data, &bytes_to_fill, packetizer->cb.user_arg);

        if (GOLIOTH_BLE_GATT_PACKETIZER_ERROR == cb_result)
        {
            packetizer->error = ENODATA;
            *dst_len = 0;
            ret = GOLIOTH_BLE_GATT_PACKETIZER_ERROR;
            goto finish;
        }

        *dst_len = sizeof(struct golioth_ble_gatt_packet) + bytes_to_fill;

        if (GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA == cb_result)
        {
            pkt->ctrl = GOLIOTH_BLE_GATT_PACKET_END;
            ret = GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA;
        }
    }

finish:
    return ret;
}

int golioth_ble_gatt_packetizer_error(struct golioth_ble_gatt_packetizer *packetizer)
{
    return packetizer->error;
}

void golioth_ble_gatt_packetizer_finish(struct golioth_ble_gatt_packetizer *packetizer)
{
    free(packetizer);
}
