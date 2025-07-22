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

void pouch_event_emit(enum pouch_event event)
{
    STRUCT_SECTION_FOREACH(pouch_event_handler, handler)
    {
        handler->callback(event, handler->ctx);
    }
}

static int pouch_module_init(void)
{
    k_work_queue_init(&pouch_work_q);

    k_work_queue_start(&pouch_work_q,
                       pouch_stack,
                       K_THREAD_STACK_SIZEOF(pouch_stack),
                       CONFIG_POUCH_THREAD_PRIORITY,
                       NULL);

    return 0;
}
SYS_INIT(pouch_module_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

int pouch_init(const struct pouch_config *config)
{
    downlink_init(&pouch_work_q);
    uplink_init();

    return crypto_init(config);
}
