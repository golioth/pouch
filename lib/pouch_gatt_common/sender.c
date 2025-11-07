/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <limits.h>
#include <stdlib.h>

#include <zephyr/sys/atomic.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/sender.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gatt_sender, CONFIG_POUCH_GATT_COMMON_LOG_LEVEL);

enum
{
    SENDER_FLAG_SENDING,
    SENDER_FLAG_COMPLETE,
    SENDER_FLAG_COUNT,
};

struct pouch_gatt_sender
{
    atomic_t last_sent;
    atomic_t last_acknowledged;
    atomic_t offered_window;
    ATOMIC_DEFINE(flags, SENDER_FLAG_COUNT);
    struct pouch_gatt_packetizer *packetizer;
    pouch_gatt_send_fn send;
    void *send_arg;
    void *buffer;
    size_t buffer_size;
};

static int calculate_outstanding_packets(const struct pouch_gatt_sender *sender)
{
    const uint32_t modulus = 1 << (CHAR_BIT * sizeof(uint8_t));
    return (modulus + atomic_get(&sender->last_sent) - atomic_get(&sender->last_acknowledged))
        % modulus;
}

static int calculate_usable_window(const struct pouch_gatt_sender *sender)
{
    return atomic_get(&sender->offered_window) - calculate_outstanding_packets(sender);
}

static int send_data(struct pouch_gatt_sender *sender)
{
    int err = 0;

    if (atomic_test_and_set_bit(sender->flags, SENDER_FLAG_SENDING))
    {
        return 0;
    }

    int packets_sent = 0;

    while (calculate_usable_window(sender) > 0
           && !atomic_test_bit(sender->flags, SENDER_FLAG_COMPLETE)
           && packets_sent < CONFIG_BT_ATT_TX_COUNT)
    {
        size_t len = sender->buffer_size;
        enum pouch_gatt_packetizer_result ret =
            pouch_gatt_packetizer_get(sender->packetizer, sender->buffer, &len);

        if (POUCH_GATT_PACKETIZER_ERROR == ret)
        {
            err = -1 * pouch_gatt_packetizer_error(sender->packetizer);
            LOG_WRN("Packetizer error: %d", err);
            break;
        }

        if (POUCH_GATT_PACKETIZER_EMPTY_PAYLOAD == ret)
        {
            LOG_DBG("No data available to send");
            err = -EAGAIN;
            break;
        }

        int seq = pouch_gatt_packetizer_get_sequence(sender->buffer, len);

        LOG_DBG("Sending packet %d. Outstanding: %d Offered: %ld Usable: %d",
                seq,
                calculate_outstanding_packets(sender),
                atomic_get(&sender->offered_window),
                calculate_usable_window(sender));

        err = sender->send(sender->send_arg, sender->buffer, len);
        if (err)
        {
            LOG_ERR("Error sending data: %d", err);
            break;
        }

        atomic_set(&sender->last_sent, seq);

        if (POUCH_GATT_PACKETIZER_NO_MORE_DATA == ret)
        {
            LOG_DBG("No more data");
            /* Set flag only after successful send of last packet */
            atomic_set_bit(sender->flags, SENDER_FLAG_COMPLETE);
        }

        packets_sent++;
    }

    atomic_clear_bit(sender->flags, SENDER_FLAG_SENDING);

    return err;
}

int pouch_gatt_sender_send_fin(pouch_gatt_send_fn send,
                               void *send_arg,
                               enum pouch_gatt_ack_code code)
{
    uint8_t fin[POUCH_GATT_FIN_SIZE];
    ssize_t ret = pouch_gatt_packetizer_fin_encode(fin, sizeof(fin), code);
    if (0 > ret)
    {
        LOG_ERR("Could not send FIN: %d", ret);
        return ret;
    }

    return send(send_arg, fin, ret);
}

int pouch_gatt_sender_receive_ack(struct pouch_gatt_sender *sender,
                                  const void *data,
                                  size_t length,
                                  bool *complete)
{
    unsigned int last_acknowledged;
    unsigned int offered_window;

    *complete = false;

    enum pouch_gatt_ack_code code;
    int err = pouch_gatt_ack_decode(data, length, &code, &last_acknowledged, &offered_window);
    if (err)
    {
        goto finish;
    }
    if (code != POUCH_GATT_ACK)
    {
        err = code;
        goto finish;
    }

    LOG_DBG("Received ack for packet %d", last_acknowledged);

    atomic_set(&sender->last_acknowledged, last_acknowledged);
    atomic_set(&sender->offered_window, offered_window);

    if (!atomic_test_bit(sender->flags, SENDER_FLAG_COMPLETE)
        && calculate_usable_window(sender) > 0)
    {
        err = send_data(sender);
        if (err == -EAGAIN)
        {
            /* No data to send, don't forward to caller */
            err = 0;
        }
    }

    if (atomic_test_bit(sender->flags, SENDER_FLAG_COMPLETE)
        && atomic_get(&sender->last_sent) == last_acknowledged)
    {
        *complete = true;
        err = pouch_gatt_sender_send_fin(sender->send, sender->send_arg, POUCH_GATT_ACK);
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

    atomic_set(&sender->last_sent, UINT8_MAX);
    atomic_set(&sender->last_acknowledged, UINT8_MAX);
    atomic_set(&sender->offered_window, 0);
    atomic_clear(sender->flags);

    return sender;
}
