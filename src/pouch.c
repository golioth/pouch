/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "block.h"
#include "packet.h"
#include "uplink.h"

#include <pouch/events.h>
#include <pouch/transport/uplink.h>
#include <pouch/types.h>

#include <stdlib.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/ring_buffer.h>

static int write_header(const char *path,
                        enum pouch_content_type content_type,
                        size_t data_len,
                        k_timeout_t timeout)
{
    int err;
    size_t path_len = strlen(path);
    size_t buf_len = POUCH_ENTRY_HEADER_OVERHEAD + path_len;
    uint8_t *buf = malloc(buf_len);

    ssize_t len = entry_header_write(buf, buf_len, path, content_type, data_len);
    if (len < 0)
    {
        err = len;
        goto out;
    }

    err = block_write(buf, len, timeout);

out:
    free(buf);
    return err;
}


int pouch_uplink_entry_write(const char *path,
                             enum pouch_content_type content_type,
                             const void *data,
                             size_t len,
                             k_timeout_t timeout)
{
    if (path == NULL || data == NULL || len == 0)
    {
        return -EINVAL;
    }

    int err = block_lock(timeout);
    if (err)
    {
        return err;
    }

    err = write_header(path, content_type, len, timeout);
    if (err)
    {
        goto out;
    }

    err = block_write(data, len, timeout);

out:
    block_release();
    return err;
}

void pouch_event_emit(enum pouch_event event)
{
    STRUCT_SECTION_FOREACH(pouch_event_handler, handler)
    {
        handler->callback(event, handler->ctx);
    }
}

int pouch_uplink_clear(void)
{
    return uplink_reset();
}

size_t pouch_uplink_pending(void)
{
    return uplink_pending();
}

int pouch_init(void)
{
    return uplink_init();
}

SYS_INIT(pouch_init, APPLICATION, 0);
