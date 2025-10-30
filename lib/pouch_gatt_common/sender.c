/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <zephyr/sys/atomic.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/sender.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gatt_sender, LOG_LEVEL_DBG);

enum
{
    DOWNLINK_FLAG_SENDING,
    DOWNLINK_FLAG_COMPLETE,
    DOWNLINK_FLAG_COUNT,
};

struct pouch_gatt_sender
{
    atomic_t last_sent;
    atomic_t last_acknowledged;
    atomic_t offered_window;
    ATOMIC_DEFINE(flags, DOWNLINK_FLAG_COUNT);
    struct pouch_gatt_packetizer *packetizer;
    pouch_gatt_send_fn send;
    void *send_arg;
    void *buffer;
    size_t buffer_size;
};

static int calculate_outstanding_packets(const struct pouch_gatt_sender *sender)
{
    return (256 + atomic_get(&sender->last_sent) - atomic_get(&sender->last_acknowledged)) % 256;
}

static int calculate_usable_window(const struct pouch_gatt_sender *sender)
{
    return atomic_get(&sender->offered_window) - calculate_outstanding_packets(sender);
}

static int send_data(struct pouch_gatt_sender *sender)
{
    int err = 0;

    if (atomic_test_and_set_bit(sender->flags, DOWNLINK_FLAG_SENDING))
    {
        return 0;
    }

    if (atomic_test_bit(sender->flags, DOWNLINK_FLAG_COMPLETE))
    {
        /* TODO: Is this test necessary ?? */

        goto finish;
    }

    int packets_sent = 0;

    while (calculate_usable_window(sender) > 0
           && !atomic_test_bit(sender->flags, DOWNLINK_FLAG_COMPLETE))
    {
        size_t len = sender->buffer_size;
        enum pouch_gatt_packetizer_result ret =
            pouch_gatt_packetizer_get(sender->packetizer, sender->buffer, &len);

        if (POUCH_GATT_PACKETIZER_ERROR == ret)
        {
            err = pouch_gatt_packetizer_error(sender->packetizer);
            LOG_DBG("Packetizer error: %d", err);
            goto finish;
        }

        if (POUCH_GATT_PACKETIZER_EMPTY_PAYLOAD == ret)
        {
            LOG_DBG("No data available to send");
            err = -ENODATA;
            goto finish;
        }

        if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == ret)
        {
            LOG_DBG("No more data");
            atomic_set_bit(sender->flags, DOWNLINK_FLAG_COMPLETE);
        }

        atomic_set(&sender->last_sent, pouch_gatt_packetizer_get_sequence(sender->buffer, len));

        LOG_DBG("Sending packet %ld. Outstanding: %d Offered: %ld Usable: %d",
                atomic_get(&sender->last_sent),
                calculate_outstanding_packets(sender),
                atomic_get(&sender->offered_window),
                calculate_usable_window(sender));

        err = sender->send(sender->send_arg, sender->buffer, len);
        if (err)
        {
            break;
        }

        packets_sent++;
    }

finish:
    atomic_clear_bit(sender->flags, DOWNLINK_FLAG_SENDING);

    return err;
}

int pouch_gatt_sender_receive_ack(struct pouch_gatt_sender *sender,
                                  const void *data,
                                  size_t length,
                                  bool *complete)
{
    int last_acknowledged;
    int offered_window;

    *complete = false;

    int err = pouch_gatt_ack_decode(data, length, &last_acknowledged, &offered_window);
    if (err)
    {
        goto finish;
    }

    atomic_set(&sender->last_acknowledged, last_acknowledged);
    atomic_set(&sender->offered_window, offered_window);

    if (!atomic_test_bit(sender->flags, DOWNLINK_FLAG_COMPLETE)
        && calculate_usable_window(sender) > 0)
    {
        err = send_data(sender);
        if (err == -ENODATA)
        {
            /* No data to send, don't forward to caller */
            err = 0;
        }
    }

    if (atomic_test_bit(sender->flags, DOWNLINK_FLAG_COMPLETE)
        && atomic_get(&sender->last_sent) == last_acknowledged)
    {
        *complete = true;
    }

finish:

    return err;
}

int pouch_gatt_sender_data_available(struct pouch_gatt_sender *sender)
{
    return send_data(sender);
}

void pouch_gatt_sender_destroy(struct pouch_gatt_sender *sender)
{
    free(sender->buffer);
    free(sender);
}

struct pouch_gatt_sender *pouch_gatt_sender_create(struct pouch_gatt_packetizer *packetizer,
                                                   pouch_gatt_send_fn send,
                                                   void *send_arg,
                                                   size_t mtu)
{
    struct pouch_gatt_sender *sender = malloc(sizeof(struct pouch_gatt_sender));
    if (!sender)
    {
        return NULL;
    }

    sender->buffer = malloc(mtu);
    if (!sender->buffer)
    {
        free(sender);
        return NULL;
    }

    sender->buffer_size = mtu;
    sender->packetizer = packetizer;
    sender->send = send;
    sender->send_arg = send_arg;

    atomic_set(&sender->last_sent, 255);
    atomic_set(&sender->last_acknowledged, 255);
    atomic_set(&sender->offered_window, 0);
    atomic_clear(sender->flags);

    return sender;
}