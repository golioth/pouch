/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include "mocks/transport.h"
#include <pouch/transport/uplink.h>
#include <pouch/uplink.h>
#include <pouch/types.h>

#include <zephyr/ztest.h>

void transport_session_start(void)
{
    zassert_ok(pouch_uplink_start(), "session failed to start");
    zassert_ok(pouch_uplink_pouch_open(), "pouch failed to open");
}

void transport_session_end(void)
{
    pouch_uplink_pouch_close();
    pouch_uplink_finish();
}

int transport_pouch_open(void)
{
    return pouch_uplink_pouch_open();
}

int transport_pouch_close(void)
{
    return pouch_uplink_pouch_close();
}

enum pouch_result transport_pull_data(uint8_t *dst, size_t *len)
{
    return pouch_uplink_fill(dst, len);
}

void transport_flush(void) {}

void transport_reset(void *unused)
{
    // purge the uplink:
    (void) pouch_uplink_start();
    pouch_uplink_close(K_NO_WAIT);
    // let processing run:
    k_sleep(K_MSEC(1));

    while (true)
    {
        uint8_t buf[CONFIG_POUCH_BLOCK_SIZE];
        size_t len = CONFIG_POUCH_BLOCK_SIZE;
        if (pouch_uplink_fill(buf, &len) != POUCH_MORE_DATA || len == 0)
        {
            break;
        }
    }

    pouch_uplink_finish();

    // let processing run:
    k_sleep(K_MSEC(1));
}
