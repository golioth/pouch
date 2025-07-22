/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <pouch/events.h>
#include <pouch/pouch.h>
#include "mocks/transport.h"

static uint32_t start_events;
static uint32_t end_events;

#define DEVICE_ID "test-device-id"

static const struct pouch_config pouch_config = {
    .device_id = DEVICE_ID,
};

static void *init_pouch(void)
{
    pouch_init(&pouch_config);
    return NULL;
}

ZTEST_SUITE(events, NULL, init_pouch, NULL, NULL, NULL);

K_SEM_DEFINE(event_rcvd, 0, UINT16_MAX);

#define EVENT_TIMEOUT K_SECONDS(1)

static void event_handler(enum pouch_event event, void *ctx)
{
    switch (event)
    {
        case POUCH_EVENT_SESSION_START:
            start_events++;
            break;
        case POUCH_EVENT_SESSION_END:
            end_events++;
            break;
        default:
            zassert_unreachable("Unexpected event %d", event);
            break;
    }

    k_sem_give(&event_rcvd);
}

POUCH_EVENT_HANDLER(event_handler, NULL);

ZTEST(events, test_start_event)
{
    start_events = 0;

    transport_session_start();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
    zassert_equal(start_events, 1);
    transport_session_end();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
}


ZTEST(events, test_end_event)
{
    end_events = 0;

    transport_session_start();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
    transport_session_end();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
    zassert_equal(end_events, 1);
}
