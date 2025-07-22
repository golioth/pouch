/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "downlink.h"
#include "uplink.h"
#include "crypto.h"

#include <pouch/events.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/ring_buffer.h>

K_THREAD_STACK_DEFINE(pouch_stack, CONFIG_POUCH_THREAD_STACK_SIZE);

static struct k_work_q pouch_work_q;
static struct k_work event_work;

K_MSGQ_DEFINE(pouch_event_q, sizeof(enum pouch_event), CONFIG_POUCH_EVENT_QUEUE_DEPTH, 1);

static void dispatch_events(struct k_work *work)
{
    enum pouch_event event;

    while (0 == k_msgq_get(&pouch_event_q, &event, K_NO_WAIT))
    {
        STRUCT_SECTION_FOREACH(pouch_event_handler, handler)
        {
            handler->callback(event, handler->ctx);
        }
    }
}

void pouch_event_emit(enum pouch_event event)
{
    k_msgq_put(&pouch_event_q, &event, K_NO_WAIT);

    k_work_submit_to_queue(&pouch_work_q, &event_work);
}

int pouch_init(const struct pouch_config *config)
{
    k_work_queue_init(&pouch_work_q);

    k_work_queue_start(&pouch_work_q,
                       pouch_stack,
                       K_THREAD_STACK_SIZEOF(pouch_stack),
                       CONFIG_POUCH_THREAD_PRIORITY,
                       NULL);

    k_work_init(&event_work, dispatch_events);

    downlink_init(&pouch_work_q);
    uplink_init();

    return crypto_init(config);
}
