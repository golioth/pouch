/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <zephyr/sys/iterable_sections.h>

/**
 * @file events.h
 * @brief Pouch event signalling
 */

/**
 * Pouch event types
 */
enum pouch_event
{
    /** Ready to push uplink pouches. */
    POUCH_EVENT_UPLINK_READY,
    /** All uplink pouch data has been sent to the gateway */
    POUCH_EVENT_UPLINK_COMPLETE,
};

/**
 * Pouch event callback
 *
 * @param event The event that has occurred
 * @param ctx The context provided when the event handler was registered
 */
typedef void (*pouch_event_cb)(enum pouch_event event, void *ctx);

/**
 * Pouch event handler
 *
 * This structure is used to register a callback for a specific event.
 * Use the @ref POUCH_EVENT_HANDLER macro to register an event handler.
 */
struct pouch_event_handler
{
    pouch_event_cb callback;
    void *ctx;
};

/**
 * Register an event handler
 *
 * @param _callback The callback function to be called when the event occurs
 * @param _ctx The context to be passed to the callback
 */
#define POUCH_EVENT_HANDLER(_callback, _ctx) \
    static const STRUCT_SECTION_ITERABLE(    \
        pouch_event_handler,                 \
        CONCAT(_pouch_event_handler_, _callback)) = {.callback = _callback, .ctx = _ctx};
