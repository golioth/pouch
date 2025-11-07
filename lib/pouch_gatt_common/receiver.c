/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include <pouch/transport/gatt/common/packetizer.h>
#include <pouch/transport/gatt/common/receiver.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gatt_receiver, CONFIG_POUCH_GATT_COMMON_LOG_LEVEL);

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

static void reset_ack_timer(struct pouch_gatt_receiver *receiver)
{
    k_work_reschedule(&receiver->ack_work, K_MSEC(CONFIG_POUCH_GATT_ACK_TIMEOUT_MS));
}

static int send_ack(struct pouch_gatt_receiver *receiver, enum pouch_gatt_ack_code code)
{
    uint8_t ack[POUCH_GATT_ACK_SIZE];
    ssize_t res =
        pouch_gatt_ack_encode(ack, sizeof(ack), code, receiver->last_received, receiver->window);
    /* Should only fail if sizeof(ack) is small (i.e. a constant is less than a constant) */
    __ASSERT(res > 0, "ack buffer too small");

    int err = receiver->send_ack(receiver->send_ack_arg, ack, res);
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

    LOG_DBG("Sending ack from timer, last_acked: %d, last_received: %d",
            receiver->last_acknowledged,
            receiver->last_received);

    int err = send_ack(receiver, POUCH_GATT_ACK);
    if (err)
    {
        LOG_ERR("Error sending ACK: %d", err);
    }
}

static void reset_receiver_context(struct pouch_gatt_receiver *receiver)
{
    receiver->last_received = UINT8_MAX;
    receiver->last_acknowledged = UINT8_MAX;
    receiver->complete = false;
}

int pouch_gatt_receiver_send_nack(pouch_gatt_send_ack_fn send_ack,
                                  void *send_ack_arg,
                                  enum pouch_gatt_ack_code code)
{
    uint8_t ack[POUCH_GATT_ACK_SIZE];
    pouch_gatt_ack_encode(ack, sizeof(ack), code, 0, 0);
    return send_ack(send_ack_arg, ack, sizeof(ack));
}

int pouch_gatt_receiver_receive_data(struct pouch_gatt_receiver *receiver,
                                     const void *data,
                                     size_t length,
                                     bool *complete)
{
    int err = 0;
    bool is_first = false;
    bool is_last = false;
    const void *payload = NULL;
    unsigned int seq = 0;
    *complete = false;
    ssize_t payload_len =
        pouch_gatt_packetizer_decode(data, length, &payload, &is_first, &is_last, &seq);

    if (0 > payload_len)
    {
        LOG_ERR("Received bad packet: %d", payload_len);
        err = -EBADMSG;
        goto finish;
    }

    enum pouch_gatt_ack_code code;
    if (pouch_gatt_packetizer_is_fin(data, length, &code))
    {
        LOG_DBG("Received FIN (%d)", code);
        if (!receiver->complete)
        {
            err = -ECONNRESET;
            LOG_WRN("Received FIN before last packet");
        }
        else if (POUCH_GATT_ACK != code)
        {
            err = -EPROTO;
            LOG_WRN("Received FIN with error code (%d)", code);
        }

        *complete = true;

        reset_receiver_context(receiver);

        return err;
    }

    if (seq != (receiver->last_received + 1) % (UINT8_MAX + 1))
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


finish:
    err = send_ack(receiver, err ? POUCH_GATT_NACK_UNKNOWN : POUCH_GATT_ACK);
    return err;
}

void pouch_gatt_receiver_destroy(struct pouch_gatt_receiver *receiver)
{
    struct k_work_sync sync;
    k_work_cancel_delayable_sync(&receiver->ack_work, &sync);
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
        receiver->window = window;

        reset_receiver_context(receiver);

        k_work_init_delayable(&receiver->ack_work, ack_timer_handler);

        reset_ack_timer(receiver);
    }

    return receiver;
}
