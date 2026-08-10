/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <pouch/port.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file fw.h
 * @brief MCU-mediated firmware relay over the Pouch Serial firmware channel.
 *
 * On MPU+MCU parts the MCU usually has no flash of its own: it is loaded from
 * the host filesystem at every boot (remoteproc on Linux). The MCU is still the
 * authenticated Pouch endpoint, so it downloads its own image through Pouch OTA
 * and streams the plaintext out to the host, which verifies and applies it.
 *
 * The application feeds this module from its Golioth OTA handlers; the serial
 * firmware endpoint drains it. Only a small buffer of MCU RAM is used - the
 * image is never resident.
 *
 * The image arrives from the cloud over several Pouch sessions, so the relay is
 * chunked: each session carries whatever is buffered at that moment, tagged with
 * its absolute offset in the image, and the host assembles them. A relay that
 * tried to send the whole image within one session would deadlock, because the
 * remaining bytes only arrive on the next session's downlink.
 */

/** Size of the fixed part of the firmware chunk header. */
#define POUCH_SERIAL_FW_HDR_FIXED_LEN 46

/** Magic at the head of a firmware chunk: "PFW2", little endian. */
#define POUCH_SERIAL_FW_MAGIC 0x32574650UL

/** Firmware apply result reported by the broker. */
enum pouch_serial_fw_status
{
    POUCH_SERIAL_FW_STATUS_OK = 0,       /**< Verified and installed */
    POUCH_SERIAL_FW_STATUS_HASH_FAIL = 1, /**< SHA-256 mismatch, nothing installed */
    POUCH_SERIAL_FW_STATUS_ERROR = 2,    /**< Install or I/O error */
};

/**
 * Announce a firmware image that is about to be relayed.
 *
 * Call once, from the OTA manifest handler, before any @ref pouch_serial_fw_write.
 * The details are sent to the broker ahead of the image so it can verify what it
 * receives.
 *
 * @param package  Component/package name.
 * @param version  Target version string.
 * @param size     Total image size in bytes.
 * @param sha256   SHA-256 of the image, 32 bytes.
 *
 * @return 0 on success, -EBUSY if a relay is already in progress, -EINVAL on bad input.
 */
int pouch_serial_fw_begin(const char *package,
                          const char *version,
                          uint32_t size,
                          const uint8_t sha256[32]);

/**
 * Relay a chunk of the firmware image.
 *
 * Blocks while the outgoing buffer is full, which is what applies backpressure to
 * the OTA download - call it straight from the component receive callback.
 *
 * @param buf      Image bytes.
 * @param len      Number of bytes.
 * @param timeout  How long to wait for buffer space.
 *
 * @return Number of bytes accepted, or a negative error code.
 */
int pouch_serial_fw_write(const void *buf, size_t len, pouch_timeout_t timeout);

/**
 * Mark the firmware image complete.
 *
 * Call when the OTA component reports its last block. The relay finishes once the
 * broker has drained the remaining buffered bytes.
 */
void pouch_serial_fw_end(void);

/**
 * Abandon the relay in progress.
 *
 * Discards anything buffered and returns the module to idle so a later offer of
 * the same or another image can start cleanly.
 */
void pouch_serial_fw_abort(void);

/**
 * Check whether a firmware relay is currently in progress.
 */
bool pouch_serial_fw_active(void);

/**
 * Check whether the relay buffer is too full to accept much more.
 *
 * Transports use this to stop pulling firmware downlink off the wire while the
 * relay is backed up, so the backpressure reaches the broker instead of piling
 * up as queued blocks on the device. Without it a fast link can push an entire
 * component faster than the relay drains, and the downlink runs the device out
 * of memory.
 */
bool pouch_serial_fw_pressure(void);

/**
 * Get the last status reported by the broker.
 *
 * @param status  Written with the last received status.
 * @return true if a status has been received since the last @ref pouch_serial_fw_begin.
 */
bool pouch_serial_fw_status_get(enum pouch_serial_fw_status *status);
