/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#pragma once

#include <pouch/events.h>
#include <pouch/types.h>
#include <psa/crypto_sizes.h>

#define PUBKEY_LEN PSA_EXPORT_PUBLIC_KEY_MAX_SIZE

/** Role of pouch participant */
enum pouch_role
{
    /** Device */
    POUCH_ROLE_DEVICE,
    /** Server */
    POUCH_ROLE_SERVER,
};

/** Pouch identifier */
typedef uint16_t pouch_id_t;

/** Public key buffer */
struct pubkey
{
    /** Raw public key data */
    uint8_t data[PUBKEY_LEN];
    /** Length of public key */
    size_t len;
};

/** Signal event listeners */
void pouch_event_emit(enum pouch_event event);
