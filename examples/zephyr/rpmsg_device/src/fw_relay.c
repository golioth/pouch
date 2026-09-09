/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Relay backend: download the image through Pouch OTA and stream the plaintext
 * out to the host over the serial firmware channel. The host verifies the
 * SHA-256 from the manifest, installs the image next to the previous one, and
 * restarts this core.
 *
 * Only a block-sized buffer of RAM is used: relaying blocks while the outgoing
 * buffer is full, which throttles the download to the speed of the link.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fw_relay, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include <golioth/ota.h>
#include <pouch/transport/serial/fw.h>

#include "fw_backend.h"

#define FW_COMPONENT CONFIG_EXAMPLE_FW_COMPONENT

static bool relaying;

int fw_relay_start(const struct fw_pending *pending)
{
    int err = pouch_serial_fw_begin(FW_COMPONENT,
                                    pending->version,
                                    (uint32_t) pending->size,
                                    pending->hash);
    if (err)
    {
        LOG_ERR("Failed to start firmware relay: %d", err);
        return err;
    }

    relaying = true;
    golioth_ota_mark_for_download(FW_COMPONENT);
    return 0;
}

void fw_relay_abort(void)
{
    pouch_serial_fw_abort();
    relaying = false;
}

/* Runs on the downlink decrypt work queue. Blocking here is deliberate: it is
 * the backpressure that stops the cloud outrunning the serial link.
 */
void fw_relay_receive(const void *data, size_t offset, size_t len, bool is_last)
{
    if (!relaying)
    {
        return;
    }

    /* Blocking here is the backpressure that stops the cloud outrunning the
     * link: the broker drains the firmware channel concurrently with this
     * downlink, so waiting does make progress. It is still bounded rather than
     * indefinite - a broker that never collects the firmware channel would
     * otherwise wedge the session that is supposed to drain it.
     */
    int ret = pouch_serial_fw_write(data, len, POUCH_SECONDS(5));
    if (ret < (int) len)
    {
        LOG_ERR("Firmware relay stalled at offset %zu (%d/%zu) - abandoning update",
                offset,
                ret,
                len);
        relaying = false;
        pouch_serial_fw_abort();
        golioth_ota_mark_idle(FW_COMPONENT);
        return;
    }

    if (is_last)
    {
        LOG_INF("Relayed %zu bytes, awaiting host apply", offset + len);
        pouch_serial_fw_end();
        relaying = false;

        /* The host verifies and restarts this core. Marking updating stops the
         * cloud sending more component data in the meantime.
         */
        golioth_ota_mark_updating(FW_COMPONENT);
    }
}
