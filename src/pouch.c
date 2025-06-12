/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "uplink.h"

#include "crypto.h"

#include <pouch/events.h>
#include <pouch/transport/uplink.h>
#include <pouch/types.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
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
    int err = crypto_init(config);
    if (err)
    {
        return err;
    }

    return uplink_init(config);
}
