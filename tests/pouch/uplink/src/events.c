/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <pouch/events.h>
#include <pouch/events.h>
#include "mocks/transport.h"

static uint32_t start_events;
static uint32_t end_events;

ZTEST_SUITE(events, NULL, NULL, NULL, NULL, NULL);

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
}

POUCH_EVENT_HANDLER(event_handler, NULL);

ZTEST(events, test_start_event)
{
    start_events = 0;

    transport_session_start();
    zassert_equal(start_events, 1);
    transport_session_end();
}


ZTEST(events, test_end_event)
{
    end_events = 0;

    transport_session_start();
    transport_session_end();
    zassert_equal(end_events, 1);
}
