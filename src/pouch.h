/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <pouch/events.h>
#include <pouch/types.h>

#define POUCH_VERSION 1

enum pouch_direction
{
    /** Uplink direction */
    POUCH_UPLINK,
    /** Downlink direction */
    POUCH_DOWNLINK,
};

enum pouch_role
{
    /** Device */
    POUCH_ROLE_DEVICE,
    /** Server */
    POUCH_ROLE_SERVER,
};


/** Pouch identifier */
typedef uint32_t pouch_id_t;

/** Signal event listeners */
void pouch_event_emit(enum pouch_event event);
