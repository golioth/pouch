/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "../bearer.h"
#include <pouch/transport/types.h>
#include "../endpoints/endpoint.h"

struct pouch_sender
{
    const struct pouch_endpoint *endpoint;
    struct pouch_bearer *bearer;

    /**
     * Ring of CONFIG_POUCH_TRANSPORT_SAR_TX_WINDOW slots, each holding an encoded fragment
     * prefixed by its length, kept until the receiver ACKs it.
     */
    uint8_t *buf;

    /*
     * Fragment counters since the transfer started. The fragment's seq is the counter's lower
     * bits. base <= next <= pulled <= base + CONFIG_POUCH_TRANSPORT_SAR_TX_WINDOW.
     */

    /**
     * First fragment the receiver hasn't ACKed. Once the FIN has been sent, it's the fragment
     * after the last one.
     */
    uint32_t base;
    /** Next fragment to send. */
    uint32_t next;
    /** Next fragment to pull from the endpoint. */
    uint32_t pulled;
    /** End of the receiver's window. */
    uint32_t window;

    /** Number of times the sender went back without the receiver making progress. */
    uint8_t retries;
    /** The receiver's first ACK has been received. */
    bool got_ack;
    uint8_t state;
};

int pouch_sender_open(struct pouch_sender *sender, struct pouch_bearer *bearer);
void pouch_sender_ready(struct pouch_sender *sender);
int pouch_sender_recv(struct pouch_sender *sender, const uint8_t *buf, size_t len);
void pouch_sender_close(struct pouch_sender *sender);
