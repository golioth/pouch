/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <errno.h>
#include "receiver.h"
#include "packet.h"
#include <pouch/port.h>
#include <zephyr/kernel.h>

POUCH_LOG_REGISTER(pouch_receiver, CONFIG_POUCH_TRANSPORT_LOG_LEVEL);

enum state
{
    STATE_IDLE,
    STATE_READY,
    STATE_ACTIVE,
    STATE_ENDED,
    STATE_FAILED,
};

static void end(struct pouch_receiver *p, bool success)
{
    POUCH_LOG_DBG("Ending transfer %p: %s", p, success ? "success" : "fail");
    if (p->endpoint->end)
    {
        p->endpoint->end(p->bearer, success);
    }
    p->state = success ? STATE_IDLE : STATE_FAILED;

    pouch_bearer_close(p->bearer, success);
}

static void schedule_ack(struct pouch_receiver *p)
{
    k_work_schedule(&p->work, K_MSEC(CONFIG_POUCH_TRANSPORT_ACK_TIMEOUT_MS));
}

static void send_ack(struct k_work *work)
{
    struct pouch_receiver *p =
        CONTAINER_OF(k_work_delayable_from_work(work), struct pouch_receiver, work);
    uint8_t buf[POUCH_SAR_RX_PKT_LEN];
    struct pouch_sar_rx_pkt ack = {
        .code =
            p->state == STATE_FAILED ? POUCH_RECEIVER_CODE_NACK_UNKNOWN : POUCH_RECEIVER_CODE_ACK,
        .seq = p->seq,
        .window = p->window,
    };
    pouch_sar_rx_pkt_encode(&ack, buf);

    POUCH_LOG_DBG("Sending ack (0x%x window: %u)", ack.seq, ack.window);

    int err = pouch_bearer_send(p->bearer, buf, sizeof(buf));
    if (err)
    {
        // try again later:
        POUCH_LOG_ERR("TX failed (%d)", err);
        schedule_ack(p);
        return;
    }

    schedule_ack(p);
    p->ack = ack.seq;
}

int pouch_receiver_open(struct pouch_receiver *recv, struct pouch_bearer *bearer, uint8_t window)
{
    if (bearer->maxlen < POUCH_SAR_RX_PKT_LEN)
    {
        return -EINVAL;
    }

    if (window > POUCH_SAR_WINDOW_MAX)
    {
        return -EINVAL;
    }

    if (recv->endpoint == NULL || recv->endpoint->recv == NULL)
    {
        return -EINVAL;
    }

    if (recv->state != STATE_IDLE && recv->state != STATE_FAILED)
    {
        return -EBUSY;
    }

    POUCH_LOG_DBG("Starting transfer %p", recv);

    recv->bearer = bearer;
    recv->window = window;
    recv->seq = POUCH_SAR_SEQ_MAX;
    recv->state = STATE_READY;
    k_work_init_delayable(&recv->work, send_ack);

    if (recv->endpoint->start != NULL)
    {
        int err = recv->endpoint->start(recv->bearer);
        if (err)
        {
            // not calling end() here, as transfer never started:
            recv->state = STATE_IDLE;
            return err;
        }
    }

    k_work_schedule(&recv->work, K_NO_WAIT);

    return 0;
}

int pouch_receiver_recv(struct pouch_receiver *recv, const uint8_t *buf, size_t len)
{
    struct pouch_sar_tx_pkt pkt;

    int err = pouch_sar_tx_pkt_decode(buf, len, &pkt);
    if (err)
    {
        POUCH_LOG_ERR("Decode failed (%d)", err);
        end(recv, false);
        return err;
    }

    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        if (pkt.len != 0)
        {
            POUCH_LOG_ERR("Invalid FIN (len %d)", pkt.len);
            end(recv, false);
            return -EINVAL;
        }

        if (recv->state == STATE_IDLE || recv->state == STATE_FAILED)
        {
            POUCH_LOG_ERR("Duplicate FIN");
            return -EINVAL;
        }

        if (recv->state != STATE_ENDED)
        {
            POUCH_LOG_ERR("Unexpected FIN");
            end(recv, false);
            return -EINVAL;
        }

        bool success = !!(pkt.flags & POUCH_SAR_TX_PKT_FLAG_IDLE);

        POUCH_LOG_DBG("recv FIN (%s)", success ? "success" : "fails");
        end(recv, success);
        return 0;
    }

    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_FIRST)
    {
        if (recv->state == STATE_ACTIVE)
        {
            POUCH_LOG_ERR("Duplicate first packet");
            end(recv, false);
            return -EIO;
        }

        if (recv->state != STATE_READY)
        {
            POUCH_LOG_ERR("Invalid state (%u)", recv->state);
            end(recv, false);
            return -EIO;
        }

        recv->state = STATE_ACTIVE;
    }
    else if (recv->state != STATE_ACTIVE)
    {
        POUCH_LOG_ERR("Invalid state (%u)", recv->state);
        end(recv, false);
        return -EBUSY;
    }

    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_LAST)
    {
        if (recv->state != STATE_ACTIVE)
        {
            POUCH_LOG_ERR("Invalid state (%u)", recv->state);
            end(recv, false);
            return -EBUSY;
        }

        recv->state = STATE_ENDED;
    }

    if (pkt.seq != ((recv->seq + 1) & POUCH_SAR_SEQ_MASK))
    {
        POUCH_LOG_WRN("OoO RX: %x (last: %x)", pkt.seq, recv->seq);
        // out of order packet - should be ignored
        k_work_reschedule(&recv->work, K_NO_WAIT);  // ack last received packet instead
        return 0;
    }

    if (pkt.len > 0)
    {
        err = recv->endpoint->recv(recv->bearer, pkt.data, pkt.len);
        if (err)
        {
            POUCH_LOG_ERR("RX callback failed: %d", err);
            end(recv, false);
            k_work_reschedule(&recv->work, K_NO_WAIT);  // NACK
            return err;
        }
    }

    recv->seq = pkt.seq;

    // send ack right away:
    k_work_reschedule(&recv->work, K_NO_WAIT);

    return 0;
}

void pouch_receiver_ready(struct pouch_receiver *recv)
{
    k_work_reschedule(&recv->work, K_NO_WAIT);
}

void pouch_receiver_close(struct pouch_receiver *recv)
{
    POUCH_LOG_DBG("Finished transfer %p", recv);

    k_work_cancel_delayable(&recv->work);

    if (recv->state == STATE_ACTIVE || recv->state == STATE_READY)
    {
        // If the transfer didn't finish properly, stop it and report it as failed:
        end(recv, false);
    }
}
