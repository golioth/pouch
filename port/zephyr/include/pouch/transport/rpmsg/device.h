/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>

/**
 * @file device.h
 * @brief Pouch rpmsg transport - device side.
 *
 * The rpmsg device transport is an adapter on top of the Pouch Serial
 * transport (see @ref pouch/transport/serial/device.h). It carries the five
 * Pouch Serial logical channels (info, server-cert, device-cert, downlink,
 * uplink) over a single rpmsg endpoint: each rpmsg message carries exactly one
 * Pouch Serial frame (a 1-byte header plus payload), so no additional framing
 * or segmentation is required on this reliable, ordered link.
 *
 * On a heterogeneous MPU+MCU SoC the device runs on the MCU (e.g. a Cortex-R5
 * or Cortex-M core) and the broker runs on the Linux application processor,
 * reached over OpenAMP/rpmsg through the Zephyr @c ipc_service abstraction.
 *
 * The adapter initializes itself from devicetree at @c APPLICATION init
 * priority. It uses the ipc_service instance referenced by the
 * @c golioth,pouch-rpmsg-ipc chosen node, for example:
 *
 * @code{.dts}
 * chosen {
 *     golioth,pouch-rpmsg-ipc = &ipc0;
 * };
 * @endcode
 *
 * No application call is required to bring the transport up; the functions
 * below exist for applications and tests that want to observe or wait for the
 * endpoint to become ready.
 */

/**
 * Explicitly initialize the rpmsg device transport.
 *
 * This is normally invoked automatically via @c SYS_INIT at @c APPLICATION
 * init priority and does not need to be called by the application. It is
 * idempotent: a second call returns success without re-registering the
 * endpoint.
 *
 * @return 0 on success, negative error code on failure.
 */
int pouch_rpmsg_device_init(void);

/**
 * Check whether the rpmsg endpoint has been bound to its remote counterpart.
 *
 * Until the endpoint is bound no frames can be exchanged with the broker.
 *
 * @return true if the endpoint is bound and ready to carry Pouch frames.
 */
bool pouch_rpmsg_device_is_bound(void);
