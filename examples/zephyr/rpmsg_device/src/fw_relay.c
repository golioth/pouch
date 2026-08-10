/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MCU-mediated firmware update.
 *
 * This core has no flash of its own - the host loads it through remoteproc at
 * every boot - so it cannot apply an update itself. It is still the
 * authenticated Pouch endpoint, so it downloads its own image through Pouch OTA
 * and streams the plaintext out to the host over the serial firmware channel.
 * The host verifies the SHA-256 from the manifest, installs the image next to
 * the previous one, and restarts this core.
 *
 * Only a block-sized buffer of RAM is used: relaying blocks while the outgoing
 * buffer is full, which throttles the download to the speed of the link.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fw_relay, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include <string.h>

#include <golioth/ota.h>
#include <pouch/transport/serial/fw.h>

#include <app_version.h>

#define FW_COMPONENT CONFIG_EXAMPLE_FW_COMPONENT

static bool relaying;

/* Runs on the downlink decrypt work queue. Blocking here is deliberate: it is
 * the backpressure that stops the cloud outrunning the serial link.
 */
static void fw_receive(const void *data, size_t offset, size_t len, bool is_last)
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

GOLIOTH_OTA_COMPONENT(fw, FW_COMPONENT, APP_VERSION_STRING, fw_receive);

static void fw_manifest(const struct golioth_ota_manifest_component *components, size_t num)
{
    for (size_t i = 0; i < num; i++)
    {
        const struct golioth_ota_manifest_component *c = &components[i];

        if (strcmp(c->name, FW_COMPONENT) != 0)
        {
            continue;
        }

        if (strcmp(c->target, APP_VERSION_STRING) == 0)
        {
            LOG_DBG("Already running %s", c->target);
            continue;
        }

        LOG_INF("Update available: %s -> %s (%zu bytes)", c->current, c->target, c->size);

        int err = pouch_serial_fw_begin(c->name, c->target, (uint32_t) c->size, c->target_hash);
        if (err)
        {
            LOG_ERR("Failed to start firmware relay: %d", err);
            continue;
        }

        relaying = true;
        golioth_ota_mark_for_download(c->name);
    }
}

GOLIOTH_OTA_MANIFEST_HANDLER(fw_manifest);
