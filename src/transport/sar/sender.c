/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <errno.h>

#include "sender.h"
#include "packet.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pouch_sender, CONFIG_POUCH_TRANSPORT_LOG_LEVEL);

enum state
{
    STATE_IDLE,
    STATE_READY,
    STATE_ACTIVE,
    STATE_FIN,
};

static void send_fin(struct pouch_sender *p)
{
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN,
    };
    if (p->state == STATE_IDLE)
    {
        // This isn't the first time we're sending the FIN
        pkt.flags |= POUCH_SAR_TX_PKT_FLAG_IDLE;
    }

    size_t len = p->bearer->maxlen;
    int err = pouch_sar_tx_pkt_encode(&pkt, p->buf, &len);
    if (err)
    {
        LOG_ERR("Encode failed (%d)", err);
        return;
    }


    err = pouch_bearer_send(p->bearer, p->buf, len);
    if (err)
    {
        LOG_ERR("TX failed (%d)", err);
        return;
    }

    p->state = STATE_IDLE;
}

static void push_fragments(struct pouch_sender *sender)
{
    while (sender->seq != sender->window)
    {
        struct pouch_sar_tx_pkt pkt = {
            .seq = sender->seq,
            .data = &sender->buf[POUCH_SAR_TX_PKT_HEADER_LEN],
            .len = sender->bearer->maxlen - POUCH_SAR_TX_PKT_HEADER_LEN,
        };
        if (sender->state == STATE_READY)
        {
            pkt.flags |= POUCH_SAR_TX_PKT_FLAG_FIRST;
        }

        enum pouch_result res = sender->endpoint->send((void *) pkt.data, &pkt.len);
        if (res == POUCH_ERROR)
        {
            LOG_ERR("Error from endpoint, aborting");
            pouch_bearer_abort(sender->bearer);
            return;
        }
        if (res == POUCH_MORE_DATA && pkt.len == 0)
        {
            // no data at this time, will come back later.
            return;
        }

        if (res == POUCH_NO_MORE_DATA)
        {
            pkt.flags |= POUCH_SAR_TX_PKT_FLAG_LAST;
            LOG_DBG("Last entry");
        }

        size_t len = sender->bearer->maxlen;
        int err = pouch_sar_tx_pkt_encode(&pkt, sender->buf, &len);
        if (err)
        {
            LOG_ERR("Encode failed (%d)", err);
            return;
        }

        err = pouch_bearer_send(sender->bearer, sender->buf, len);
        if (err)
        {
            LOG_ERR("TX failed (%d)", err);
            return;
        }

        LOG_DBG("Data sent. flags: %x, len: %u, seq: %x", pkt.flags, pkt.len, pkt.seq);

        sender->seq++;
        sender->state = STATE_ACTIVE;

        if (res == POUCH_NO_MORE_DATA)
        {
            sender->state = STATE_FIN;
            return;
        }
    }
}


int pouch_sender_open(struct pouch_sender *sender, struct pouch_bearer *bearer)
{
    LOG_DBG("Starting transfer %p", sender);

    sender->bearer = bearer;
    sender->seq = 0;
    sender->window = 0;
    sender->state = STATE_READY;

    sender->buf = malloc(bearer->maxlen);
    if (sender->buf == NULL)
    {
        return -ENOMEM;
    }

    if (sender->endpoint->start != NULL)
    {
        int err = sender->endpoint->start();
        if (err)
        {
            free(sender->buf);
            sender->buf = NULL;
            return err;
        }
    }

    // wait for the receiver to send an ack with a window.

    return 0;
}

int pouch_sender_recv(struct pouch_sender *sender, const uint8_t *buf, size_t len)
{
    struct pouch_sar_rx_pkt ack;
    if (sender->bearer == NULL)
    {
        LOG_DBG("Received before opening");
        return -EBUSY;
    }

    int err = pouch_sar_rx_pkt_decode(buf, len, &ack);
    if (err)
    {
        LOG_ERR("Invalid ack (%d)", err);
        return err;
    }

    sender->window = ack.seq + ack.window + 1;

    LOG_DBG("Received ack (%x window: %u. New target seq: %x)",
            ack.seq,
            ack.window,
            sender->window);

    if (sender->state == STATE_ACTIVE || sender->state == STATE_READY)
    {
        push_fragments(sender);
    }
    else if (((ack.seq + 1) & POUCH_SAR_SEQ_MASK) == sender->seq)
    {
        send_fin(sender);
        if (sender->endpoint->end)
        {
            sender->endpoint->end(sender);
        }
    }

    return 0;
}

void pouch_sender_close(struct pouch_sender *sender)
{
    sender->state = STATE_IDLE;
    free(sender->buf);
    sender->buf = NULL;
    sender->bearer = NULL;
}
