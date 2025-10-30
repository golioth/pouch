/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <zephyr/kernel.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/receiver.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gatt_receiver, LOG_LEVEL_DBG);

#define ACK_TIMEOUT_SHORT_MS 100
#define ACK_TIMEOUT_LONG_MS 1000

struct pouch_gatt_receiver
{
    pouch_gatt_send_ack_fn send_ack;
    void *send_ack_arg;
    pouch_gatt_receiver_data_push_fn push;
    void *push_arg;
    struct k_work_delayable ack_work;
    uint8_t window;
    uint8_t last_received;
    uint8_t last_acknowledged;
    bool complete;
};

static int calculate_unacknowledged_packets(const struct pouch_gatt_receiver *receiver)
{
    return (256 + (uint16_t) receiver->last_acknowledged - (uint16_t) receiver->last_received)
        % 256;
}

static void reset_ack_timer(struct pouch_gatt_receiver *receiver)
{
    if (receiver->last_received == receiver->last_acknowledged)
    {
        k_work_reschedule(&receiver->ack_work, K_MSEC(ACK_TIMEOUT_LONG_MS));
    }
    else
    {
        k_work_reschedule(&receiver->ack_work, K_MSEC(ACK_TIMEOUT_SHORT_MS));
    }
}

static int send_ack(struct pouch_gatt_receiver *receiver)
{
    uint8_t ack[2];
    pouch_gatt_ack_encode(ack, sizeof(ack), receiver->last_received, receiver->window);
    int err = receiver->send_ack(receiver->send_ack_arg, ack, sizeof(ack));
    if (!err)
    {
        receiver->last_acknowledged = receiver->last_received;
    }

    reset_ack_timer(receiver);

    return err;
}

static void ack_timer_handler(struct k_work *work)
{
    struct k_work_delayable *delayable_work = k_work_delayable_from_work(work);

    struct pouch_gatt_receiver *receiver =
        CONTAINER_OF(delayable_work, struct pouch_gatt_receiver, ack_work);

    LOG_WRN("Sending ack from timer, last_acked: %d, last_received: %d",
            receiver->last_acknowledged,
            receiver->last_received);

    send_ack(receiver);

    reset_ack_timer(receiver);
}

int pouch_gatt_receiver_receive_data(struct pouch_gatt_receiver *receiver,
                                     const void *data,
                                     size_t length)
{
    int err = 0;
    bool is_first = false;
    bool is_last = false;
    const void *payload = NULL;
    unsigned int seq = 0;
    ssize_t payload_len =
        pouch_gatt_packetizer_decode(data, length, &payload, &is_first, &is_last, &seq);

    if (0 > payload_len)
    {
        LOG_ERR("Received bad packet: %d", payload_len);
        err = -EBADMSG;
        goto finish;
    }

    if (seq != (receiver->last_received + 1) % 256)
    {
        LOG_ERR("Received out of order packet");
        err = -EBADMSG;
        goto finish;
    }

    receiver->last_received = seq;
    receiver->complete = is_last;

    reset_ack_timer(receiver);

    err = receiver->push(receiver->push_arg, payload, payload_len, is_first, is_last);
    if (err)
    {
        goto finish;
    }

    if (is_last || calculate_unacknowledged_packets(receiver) >= receiver->window / 2)
    {
        err = send_ack(receiver);
    }

finish:
    return err;
}

void pouch_gatt_receiver_destroy(struct pouch_gatt_receiver *receiver)
{
    k_work_cancel_delayable(&receiver->ack_work);
    free(receiver);
}

struct pouch_gatt_receiver *pouch_gatt_receiver_create(pouch_gatt_send_ack_fn send_ack,
                                                       void *send_ack_arg,
                                                       pouch_gatt_receiver_data_push_fn push,
                                                       void *push_arg,
                                                       uint8_t window)
{
    struct pouch_gatt_receiver *receiver = malloc(sizeof(struct pouch_gatt_receiver));

    if (receiver)
    {
        receiver->send_ack = send_ack;
        receiver->send_ack_arg = send_ack_arg;
        receiver->push = push;
        receiver->push_arg = push_arg;
        receiver->last_received = 255;
        receiver->last_acknowledged = 255;
        receiver->window = window;
        receiver->complete = false;

        k_work_init_delayable(&receiver->ack_work, ack_timer_handler);

        reset_ack_timer(receiver);
    }

    return receiver;
}