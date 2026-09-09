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

/*
 * Signed-URL firmware handoff.
 *
 * A core with no flash of its own can have the host apply an update for it, and
 * the relay in <pouch/transport/serial/fw.h> does that by pulling the image
 * through this core and pushing the plaintext back out. That costs a buffer, and
 * it moves every byte twice over a link the host is already on.
 *
 * The alternative is to move the authority instead of the bytes. This core holds
 * the device key, so it can sign a URL for its own artifact and hand that to the
 * host; the host fetches the image directly and reports back on the shared
 * verdict channel. Nothing is buffered here and no image byte crosses the link.
 *
 * The signature is produced by a signer the application installs, rather than by
 * this module: signing means a key, and this library does not own the
 * application's key material. The signature is also made late, when the host is
 * already collecting the record, because a signed URL is valid for a short
 * window and one signed at announcement time may expire before it is asked for.
 */

/** Magic at the head of a signed-URL record: "PFU1", little endian. */
#define POUCH_SERIAL_FW_URL_MAGIC 0x31554650UL

/** Size of the fixed part of a signed-URL record. */
#define POUCH_SERIAL_FW_URL_HDR_FIXED_LEN 44

/** Magic at the head of a time record: "PTM1", little endian. */
#define POUCH_SERIAL_TIME_MAGIC 0x314D5450UL

/** Size of a time record. */
#define POUCH_SERIAL_TIME_LEN 12

/**
 * Sign a URL.
 *
 * Deliberately the shape of signy_sign_url(), so an application using that
 * library can install it directly.
 *
 * @param url            The URL to sign.
 * @param url_len        Length of @p url.
 * @param signed_url     Buffer for the signed URL.
 * @param signed_url_len Size of @p signed_url.
 * @param olen           Set to the length written to @p signed_url.
 *
 * @return 0 on success, or a negative error code.
 */
typedef int (*pouch_serial_fw_url_sign_t)(const char *url,
                                          size_t url_len,
                                          char *signed_url,
                                          size_t signed_url_len,
                                          size_t *olen);

/**
 * Install the signer used for artifact URLs.
 *
 * Without one, an announced artifact is never handed over: the record is held
 * and re-attempted, and the host simply sees nothing to collect.
 *
 * @param sign  Signer, or NULL to remove the current one.
 */
void pouch_serial_fw_url_signer_set(pouch_serial_fw_url_sign_t sign);

/**
 * Announce an artifact for the host to fetch.
 *
 * Call once, from the OTA manifest handler. @p url is the unsigned artifact
 * URL; it is signed when the host collects it, not here.
 *
 * The image is never downloaded through Pouch, so do not mark the component for
 * download. Mark it updating instead: the cloud has to stop offering it while
 * the host works, and the result arrives as an apply verdict.
 *
 * @param package  Component/package name, at most 32 bytes.
 * @param version  Target version string, at most 32 bytes.
 * @param size     Artifact size in bytes.
 * @param sha256   SHA-256 of the artifact, 32 bytes.
 * @param url      Unsigned artifact URL.
 *
 * @return 0 on success, -EBUSY if a handoff is already pending, -EINVAL on bad input.
 */
int pouch_serial_fw_url_begin(const char *package,
                              const char *version,
                              uint32_t size,
                              const uint8_t sha256[32],
                              const char *url);

/**
 * Withdraw the pending handoff.
 *
 * Returns the module to idle so a later offer can start cleanly.
 */
void pouch_serial_fw_url_abort(void);

/**
 * Check whether an artifact is announced and not yet handed over.
 */
bool pouch_serial_fw_url_active(void);

/**
 * Called when the broker reports its wall-clock time.
 *
 * Invoked from the serial receive path with no lock held. A core loaded by
 * remoteproc typically has no clock of its own, and a signed URL carries the
 * window it is valid for, so the handler is where that clock gets set.
 *
 * @param unix_seconds  Seconds since the Unix epoch, UTC.
 */
typedef void (*pouch_serial_time_cb_t)(int64_t unix_seconds);

/**
 * Register a handler for the broker's wall-clock time.
 *
 * @param cb  Handler, or NULL to remove the current one.
 */
void pouch_serial_time_callback_set(pouch_serial_time_cb_t cb);

/**
 * Check whether the broker has reported its time since boot.
 *
 * Doubles as the test for whether the host speaks this protocol at all: only a
 * broker that implements the signed-URL handoff pushes the time, so a device
 * that has not been told the time has no host to hand a URL to.
 */
bool pouch_serial_time_synced(void);
