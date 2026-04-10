/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "downlink.h"
#include "uplink.h"
#include "crypto.h"

#include <pouch/events.h>
#include <pouch/port.h>
#include <pouch/uplink.h>

POUCH_THREAD_STACK_DEFINE(pouch_stack, CONFIG_POUCH_THREAD_STACK_SIZE);

static pouch_work_q_t pouch_work_q;
static pouch_work_t event_work;

uint8_t pouch_event_q_buf[sizeof(enum pouch_event) * CONFIG_POUCH_EVENT_QUEUE_DEPTH];
pouch_msgq_t pouch_event_q;

static void dispatch_events(pouch_work_t *work)
{
    enum pouch_event event;

    while (0 == pouch_msgq_get(&pouch_event_q, &event, POUCH_TIMEOUT_NO_WAIT))
    {
        POUCH_STRUCT_SECTION_FOREACH(pouch_event_handler, handler)
        {
            handler->callback(event, handler->ctx);
        }
    }
}

void pouch_event_emit(enum pouch_event event)
{
    pouch_msgq_put(&pouch_event_q, &event, POUCH_TIMEOUT_NO_WAIT);

    pouch_work_submit_to_queue(&pouch_work_q, &event_work);
}

static void pouch_module_init(void)
{
    pouch_work_queue_init(&pouch_work_q);

    pouch_work_queue_start(&pouch_work_q,
                           pouch_stack,
                           K_THREAD_STACK_SIZEOF(pouch_stack),
                           CONFIG_POUCH_THREAD_PRIORITY,
                           "pouch_work");

    pouch_work_init(&event_work, dispatch_events);
}
POUCH_APPLICATION_STARTUP_HOOK(pouch_module_init);

int pouch_init(const struct pouch_config *config)
{
    pouch_msgq_init(&pouch_event_q,
                    pouch_event_q_buf,
                    sizeof(pouch_event_q_buf),
                    sizeof(enum pouch_event));

    int err = downlink_init(&pouch_work_q);
    if (err)
    {
        return err;
    }

    uplink_init();

    return crypto_init(config);
}
