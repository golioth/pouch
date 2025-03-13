/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "mocks/transport.h"
#include <pouch/transport/uplink.h>
#include <pouch/uplink.h>

#include <zephyr/ztest.h>

static struct pouch_uplink *uplink;

void transport_session_start(void)
{
    zassert_is_null(uplink, "uplink is not NULL");
    uplink = pouch_uplink_start();
}

void transport_session_end(void)
{
    zassert_not_null(uplink, "uplink is NULL");
    pouch_uplink_finish(uplink);
    uplink = NULL;
}

enum pouch_result transport_pull_data(uint8_t *dst, size_t *len)
{
    zassert_not_null(uplink, "uplink is NULL");
    return pouch_uplink_fill(uplink, dst, len);
}

void transport_flush(void)
{
    zassert_not_null(uplink, "uplink is NULL");
}

void transport_reset(void *unused)
{
    // purge the uplink:
    if (!uplink)
    {
        uplink = pouch_uplink_start();
        pouch_uplink_close(K_NO_WAIT);
        // let processing run:
        k_sleep(K_MSEC(1));
    }

    while (true)
    {
        uint8_t buf[CONFIG_POUCH_BLOCK_SIZE];
        size_t len = CONFIG_POUCH_BLOCK_SIZE;
        if (pouch_uplink_fill(uplink, buf, &len) != POUCH_MORE_DATA || len == 0)
        {
            break;
        }
    }

    pouch_uplink_finish(uplink);
    uplink = NULL;
}
