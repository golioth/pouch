/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <pouch/events.h>
#include <pouch/pouch.h>
#include "mocks/transport.h"

static uint32_t start_events;
static uint32_t end_events;
static uint32_t pouch_open_events;

#define DEVICE_ID "test-device-id"

static const struct pouch_config pouch_config = {
    .device_id = DEVICE_ID,
};

static void *init_pouch(void)
{
    pouch_init(&pouch_config);
    return NULL;
}

ZTEST_SUITE(events, NULL, init_pouch, NULL, transport_reset, NULL);

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
        case POUCH_EVENT_POUCH_OPEN:
            pouch_open_events++;
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
    pouch_open_events = 0;
    k_sem_reset(&event_rcvd);

    // transport_session_start() starts the session and opens the first pouch,
    // so both a session-start and a pouch-open event are emitted:
    transport_session_start();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
    zassert_equal(start_events, 1);
    zassert_equal(pouch_open_events, 1);
    transport_session_end();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);
}

ZTEST(events, test_pouch_open_event)
{
    pouch_open_events = 0;
    k_sem_reset(&event_rcvd);

    transport_session_start();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // SESSION_START
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // POUCH_OPEN
    zassert_equal(pouch_open_events, 1);

    zassert_ok(transport_pouch_close());
    zassert_ok(transport_pouch_open());
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // POUCH_OPEN
    zassert_equal(pouch_open_events, 2);

    transport_session_end();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // SESSION_END
}

ZTEST(events, test_end_event)
{
    end_events = 0;

    transport_session_start();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // SESSION_START
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // POUCH_OPEN
    transport_session_end();
    zassert_equal(k_sem_take(&event_rcvd, EVENT_TIMEOUT), 0);  // SESSION_END
    zassert_equal(end_events, 1);
}
