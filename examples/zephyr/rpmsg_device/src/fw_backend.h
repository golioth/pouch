/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pouch/transport/serial/fw.h>

/**
 * An update the cloud has offered and this core has not finished acting on.
 *
 * The manifest's strings only live for the duration of the manifest handler,
 * but an update the host rejects has to be restarted long after that, so the
 * dispatcher keeps a copy and hands it to whichever backend is in use.
 */
struct fw_pending
{
    char version[POUCH_SERIAL_FW_MAX_NAME_LEN + 1];
    uint8_t hash[32];
    size_t size;
};

/*
 * The two ways this core can be updated. Both end in the host restarting it
 * with a new image; they differ in who moves the bytes.
 */

#if defined(CONFIG_EXAMPLE_FW_RELAY)
/** Download the image through Pouch and stream the plaintext to the host. */
int fw_relay_start(const struct fw_pending *pending);
/** Abandon a relay in progress, so a later offer can start cleanly. */
void fw_relay_abort(void);
/** Feed the relay one block of the component, from the OTA callback. */
void fw_relay_receive(const void *data, size_t offset, size_t len, bool is_last);
#endif

#if defined(CONFIG_EXAMPLE_FW_SIGNED_URL)
/** True once a URL can actually be signed: credentials loaded, clock set. */
bool fw_signed_url_ready(void);
/** Hand the host a signed URL for the artifact and let it fetch the image. */
int fw_signed_url_start(const struct fw_pending *pending);
/** Withdraw a handoff that has not been collected yet. */
void fw_signed_url_abort(void);
#endif
