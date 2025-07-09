/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "downlink.h"
#include "uplink.h"
#include "crypto.h"

#include <pouch/events.h>

#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/ring_buffer.h>


void pouch_event_emit(enum pouch_event event)
{
    STRUCT_SECTION_FOREACH(pouch_event_handler, handler)
    {
        handler->callback(event, handler->ctx);
    }
}

int pouch_init(const struct pouch_config *config)
{
    downlink_init();
    uplink_init();

    return crypto_init(config);
}
