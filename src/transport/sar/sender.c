/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pouch/port.h>

#include "sender.h"
#include "packet.h"

#define SEQ(seq) ((uint8_t) ((seq) & POUCH_SAR_SEQ_MASK))

#define TX_WINDOW CONFIG_POUCH_TRANSPORT_SAR_TX_WINDOW

POUCH_LOG_REGISTER(pouch_sender, CONFIG_POUCH_TRANSPORT_LOG_LEVEL);

enum state
{
    STATE_IDLE,
    STATE_READY,
    STATE_ACTIVE,
    STATE_FIN,
    STATE_FIN_SENT,
};

static size_t slot_size(const struct pouch_bearer *bearer)
{
    return sizeof(uint16_t) + bearer->maxlen;
}

/* Each slot holds the length of the encoded fragment, followed by the fragment itself. */
static uint8_t *slot(const struct pouch_sender *sender, uint32_t fragment)
{
    return &sender->buf[(fragment % TX_WINDOW) * slot_size(sender->bearer)];
}

static uint8_t *slot_pkt(const struct pouch_sender *sender, uint32_t fragment)
{
    return slot(sender, fragment) + sizeof(uint16_t);
}

static uint16_t slot_len(const struct pouch_sender *sender, uint32_t fragment)
{
    uint16_t len;
    memcpy(&len, slot(sender, fragment), sizeof(len));
    return len;
}

static bool slot_is_last(const struct pouch_sender *sender, uint32_t fragment)
{
    struct pouch_sar_tx_pkt pkt;
    int err = pouch_sar_tx_pkt_decode(slot_pkt(sender, fragment), slot_len(sender, fragment), &pkt);
    if (err)
    {
        return false;
    }

    return (pkt.flags & POUCH_SAR_TX_PKT_FLAG_LAST);
}

static bool last_pulled(const struct pouch_sender *sender)
{
    return sender->pulled > 0 && slot_is_last(sender, sender->pulled - 1);
}

static void reset(struct pouch_sender *sender)
{
    sender->base = 0;
    sender->next = 0;
    sender->pulled = 0;
    sender->window = 0;
    sender->retries = 0;
    sender->got_ack = false;
}

static void end(struct pouch_sender *sender, bool success)
{
    if (sender->endpoint->end)
    {
        sender->endpoint->end(sender->bearer, success);
    }

    pouch_bearer_close(sender->bearer, success);

    reset(sender);
    sender->state = STATE_IDLE;

    free(sender->buf);
    sender->buf = NULL;
    sender->bearer = NULL;
}

static void send_fin(struct pouch_sender *sender)
{
    struct pouch_sar_tx_pkt pkt = {
        .flags = POUCH_SAR_TX_PKT_FLAG_FIN,
    };
    uint8_t buf[POUCH_SAR_TX_PKT_HEADER_LEN];
    size_t len = sizeof(buf);
    int err = pouch_sar_tx_pkt_encode(&pkt, buf, &len);
    if (err)
    {
        POUCH_LOG_ERR("Encode failed (%d)", err);
        return;
    }

    err = pouch_bearer_send(sender->bearer, buf, len);
    if (err)
    {
        POUCH_LOG_ERR("TX failed (%d)", err);
    }
}

/*
 * End the transfer once the receiver has ACKed the last fragment. If the FIN gets lost, the
 * receiver keeps ACKing the last fragment, so hold on to the bearer to send the FIN again, until
 * the sender is closed.
 */
static void finish(struct pouch_sender *sender)
{
    struct pouch_bearer *bearer = sender->bearer;

    send_fin(sender);

    free(sender->buf);
    sender->buf = NULL;
    // The sender may get closed from the callbacks, which puts it in the idle state.
    sender->state = STATE_FIN_SENT;

    if (sender->endpoint->end)
    {
        sender->endpoint->end(bearer, true);
    }

    pouch_bearer_close(bearer, true);
}

/*
 * Pull the next fragment from the endpoint, and encode it in its slot.
 *
 * @return 0 if a fragment was pulled, -EAGAIN if the endpoint has no data yet, or another error
 * code if the transfer failed.
 */
static int pull_fragment(struct pouch_sender *sender)
{
    uint8_t *dst = slot_pkt(sender, sender->pulled);
    struct pouch_sar_tx_pkt pkt = {
        .seq = SEQ(sender->pulled),
        .data = &dst[POUCH_SAR_TX_PKT_HEADER_LEN],
        .len = sender->bearer->maxlen - POUCH_SAR_TX_PKT_HEADER_LEN,
    };
    if (sender->pulled == 0)
    {
        pkt.flags |= POUCH_SAR_TX_PKT_FLAG_FIRST;
    }

    enum pouch_result res = sender->endpoint->send(sender->bearer, (void *) pkt.data, &pkt.len);
    if (res == POUCH_ERROR)
    {
        POUCH_LOG_ERR("Error from endpoint, aborting");
        pouch_bearer_close(sender->bearer, false);
        return -EIO;
    }
    if (res == POUCH_MORE_DATA && pkt.len == 0)
    {
        // no data at this time, will come back later.
        return -EAGAIN;
    }

    if (res == POUCH_NO_MORE_DATA)
    {
        pkt.flags |= POUCH_SAR_TX_PKT_FLAG_LAST;
        POUCH_LOG_DBG("Last entry");
    }

    size_t len = sender->bearer->maxlen;
    int err = pouch_sar_tx_pkt_encode(&pkt, dst, &len);
    if (err)
    {
        POUCH_LOG_ERR("Encode failed (%d)", err);
        return err;
    }

    uint16_t encoded_len = len;
    memcpy(slot(sender, sender->pulled), &encoded_len, sizeof(encoded_len));
    sender->pulled++;

    return 0;
}

/*
 * Send fragments until the window is full. Fragments that have been sent before are sent again
 * from their slots, and new fragments are pulled from the endpoint.
 */
static void push_fragments(struct pouch_sender *sender)
{
    uint32_t target = MIN(sender->window, sender->base + TX_WINDOW);

    while (sender->next < target)
    {
        if (sender->next == sender->pulled)
        {
            if (last_pulled(sender) || pull_fragment(sender) != 0)
            {
                return;
            }
        }

        int err = pouch_bearer_send(sender->bearer,
                                    slot_pkt(sender, sender->next),
                                    slot_len(sender, sender->next));
        if (err)
        {
            // The fragment stays in its slot, and is sent on the next attempt.
            POUCH_LOG_ERR("TX failed (%d)", err);
            return;
        }

        POUCH_LOG_DBG("Data sent. len: %u, seq: %x",
                      slot_len(sender, sender->next),
                      SEQ(sender->next));

        if (slot_is_last(sender, sender->next))
        {
            sender->state = STATE_FIN;
        }
        else if (sender->state == STATE_READY)
        {
            sender->state = STATE_ACTIVE;
        }

        sender->next++;
    }
}

int pouch_sender_open(struct pouch_sender *sender, struct pouch_bearer *bearer)
{
    if (bearer->maxlen <= POUCH_SAR_TX_PKT_HEADER_LEN || bearer->maxlen > UINT16_MAX)
    {
        return -EINVAL;
    }

    uint8_t *buf = malloc(TX_WINDOW * slot_size(bearer));
    if (buf == NULL)
    {
        return -ENOMEM;
    }

    if (sender->endpoint->start != NULL)
    {
        int err = sender->endpoint->start(bearer);
        if (err)
        {
            free(buf);
            return err;
        }
    }

    // Publish the sender only once it is fully initialized.
    sender->buf = buf;
    sender->bearer = bearer;
    reset(sender);
    sender->state = STATE_READY;

    // wait for the receiver to send an ack with a window.

    return 0;
}

int pouch_sender_recv(struct pouch_sender *sender, const uint8_t *buf, size_t len)
{
    struct pouch_sar_rx_pkt ack;
    if (sender->bearer == NULL)
    {
        POUCH_LOG_DBG("Received before opening");
        return -EBUSY;
    }

    int err = pouch_sar_rx_pkt_decode(buf, len, &ack);
    if (err)
    {
        POUCH_LOG_ERR("Invalid ack (%d)", err);
        return err;
    }

    if (sender->state == STATE_FIN_SENT)
    {
        // The transfer is over. The receiver ACKing the last fragment again means that it didn't
        // get the FIN.
        if (ack.code == POUCH_RECEIVER_CODE_ACK && ack.seq == SEQ(sender->base - 1))
        {
            POUCH_LOG_DBG("Repeating FIN");
            send_fin(sender);
        }

        return 0;
    }

    if (ack.code != POUCH_RECEIVER_CODE_ACK)
    {
        POUCH_LOG_ERR("Received NACK");
        end(sender, false);
        return -EIO;
    }

    if (ack.window > POUCH_SAR_WINDOW_MAX)
    {
        POUCH_LOG_ERR("Invalid window");
        end(sender, false);
        return -EINVAL;
    }

    // Number of fragments the ACK moves the base forward. A fragment that has been pulled may have
    // been sent before the sender went back, so the receiver may ACK it even if it isn't sent yet.
    uint32_t acked = SEQ(ack.seq + 1 - sender->base);
    if (acked > sender->pulled - sender->base)
    {
        POUCH_LOG_ERR("Out of order seq (%u, last sent: %u)", ack.seq, SEQ(sender->pulled - 1));
        end(sender, false);
        return -EINVAL;
    }

    uint32_t base = sender->base + acked;
    uint32_t window = base + ack.window;

    // If the new window ends before the current window, we're moving backwards, and should abort
    if (window < sender->window)
    {
        POUCH_LOG_ERR("Unexpected window (%u, current: %u)", SEQ(window), SEQ(sender->window));
        end(sender, false);
        return -EINVAL;
    }

    sender->window = window;

    POUCH_LOG_DBG("Received ack (%x window: %u)", ack.seq, ack.window);

    if (acked > 0 || !sender->got_ack)
    {
        sender->got_ack = true;
        sender->base = base;
        sender->retries = 0;
        if (sender->next < base)
        {
            sender->next = base;
        }

        if (base == sender->pulled && last_pulled(sender))
        {
            finish(sender);
            return 0;
        }
    }
    else if (sender->next != sender->base)
    {
        // The receiver ACKed the same fragment again, so the fragments after it were lost. Go back
        // and send them again.
        if (sender->retries == CONFIG_POUCH_TRANSPORT_SAR_MAX_RETRIES)
        {
            POUCH_LOG_ERR("No progress after %u retries, aborting", sender->retries);
            end(sender, false);
            return -EIO;
        }

        sender->retries++;
        sender->next = sender->base;
        POUCH_LOG_DBG("Going back to seq %x", SEQ(sender->next));
    }

    push_fragments(sender);

    return 0;
}

void pouch_sender_ready(struct pouch_sender *sender)
{
    if (sender->state == STATE_IDLE || sender->state == STATE_FIN_SENT)
    {
        return;
    }

    push_fragments(sender);
}

void pouch_sender_close(struct pouch_sender *sender)
{
    POUCH_LOG_DBG("%p bearer %p", sender, sender->bearer);
    switch ((enum state) sender->state)
    {
        case STATE_ACTIVE:
        case STATE_READY:
            POUCH_LOG_WRN("Closed before receiving FIN");
            end(sender, false);
            break;
        case STATE_FIN:
            end(sender, true);
            break;
        case STATE_FIN_SENT:
            // The transfer has already ended
            reset(sender);
            sender->bearer = NULL;
            sender->state = STATE_IDLE;
            break;
        case STATE_IDLE:
            POUCH_LOG_DBG("Closed while idle");
            break;
    }
}
