/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <errno.h>
#include "receiver.h"
#include "packet.h"
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pouch_receiver, CONFIG_POUCH_TRANSPORT_LOG_LEVEL);

enum state
{
    STATE_IDLE,
    STATE_ACTIVE,
    STATE_FAILED,
};

static void end(struct pouch_receiver *p, bool success)
{
    LOG_DBG("Ending transfer %p: %s", p, success ? "success" : "fail");
    if (p->endpoint->end)
    {
        p->endpoint->end(success);
    }
    p->state = success ? STATE_IDLE : STATE_FAILED;
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
        .window = p->bearer->window,
    };
    pouch_sar_rx_pkt_encode(&ack, buf);

    LOG_DBG("Sending ack (0x%x window: %u)", ack.seq, ack.window);

    int err = pouch_bearer_send(p->bearer, buf, sizeof(buf));
    if (err)
    {
        // try again later:
        LOG_ERR("TX failed (%d)", err);
        schedule_ack(p);
        return;
    }

    schedule_ack(p);
    p->ack = ack.seq;
}

int pouch_receiver_open(struct pouch_receiver *recv, struct pouch_bearer *bearer)
{
    LOG_DBG("Starting transfer %p", recv);

    recv->bearer = bearer;
    recv->seq = POUCH_SAR_SEQ_MAX;
    recv->state = STATE_ACTIVE;
    k_work_init_delayable(&recv->work, send_ack);

    if (recv->endpoint->start != NULL)
    {
        int err = recv->endpoint->start();
        if (err)
        {
            // not calling end() here, as transfer never started:
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
        LOG_ERR("Decode failed (%d)", err);
        end(recv, false);
        return err;
    }

    if (pkt.flags & POUCH_SAR_TX_PKT_FLAG_FIN)
    {
        LOG_DBG("recv FIN");
        end(recv, true);
        return 0;
    }

    if (recv->state != STATE_ACTIVE)
    {
        LOG_ERR("Invalid state (%u)", recv->state);
        return -EBUSY;
    }

    if (pkt.seq != ((recv->seq + 1) & POUCH_SAR_SEQ_MASK))
    {
        LOG_ERR("OoO RX: %x (last: %x)", pkt.seq, recv->seq);
        end(recv, false);
        k_work_reschedule(&recv->work, K_NO_WAIT);  // NACK
        return -EINVAL;
    }

    err = recv->endpoint->recv(pkt.data, pkt.len);
    if (err)
    {
        LOG_ERR("RX callback failed: %d", err);
        end(recv, false);
        k_work_reschedule(&recv->work, K_NO_WAIT);  // NACK
        return err;
    }

    recv->seq = pkt.seq;

    // send ack right away:
    k_work_reschedule(&recv->work, K_NO_WAIT);

    return 0;
}

void pouch_receiver_close(struct pouch_receiver *recv)
{
    LOG_DBG("Finished transfer %p", recv);

    k_work_cancel_delayable(&recv->work);

    if (recv->state == STATE_ACTIVE)
    {
        // If the transfer didn't finish properly, stop it and report it as failed:
        end(recv, false);
    }
}
