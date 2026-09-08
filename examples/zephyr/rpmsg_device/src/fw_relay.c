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

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <string.h>

#include <golioth/ota.h>
#include <pouch/transport/serial/fw.h>

#include <app_version.h>

#define FW_COMPONENT CONFIG_EXAMPLE_FW_COMPONENT

/* pouch_serial_fw_begin() rejects names longer than this. */
#define FW_VERSION_MAX 32

/* How many times to re-offer an image the host rejects before giving up. A
 * rejection is usually a corrupt transfer, which a retry fixes; a genuinely bad
 * image would otherwise retry every session forever.
 */
#define FW_MAX_ATTEMPTS 3

static bool relaying;

/*
 * The image currently being relayed. The manifest's strings only live for the
 * duration of the manifest handler, but a rejected image has to be restarted
 * long after that, so keep a copy.
 */
static struct
{
    char version[FW_VERSION_MAX + 1];
    uint8_t hash[32];
    size_t size;
    bool valid;
    int attempts;
} pending;

static int relay_start(void)
{
    int err =
        pouch_serial_fw_begin(FW_COMPONENT, pending.version, (uint32_t) pending.size, pending.hash);
    if (err)
    {
        LOG_ERR("Failed to start firmware relay: %d", err);
        return err;
    }

    relaying = true;
    golioth_ota_mark_for_download(FW_COMPONENT);
    return 0;
}

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

        if (strlen(c->target) > FW_VERSION_MAX)
        {
            LOG_ERR("Target version %s is too long to relay", c->target);
            continue;
        }

        strcpy(pending.version, c->target);
        memcpy(pending.hash, c->target_hash, sizeof(pending.hash));
        pending.size = c->size;
        pending.attempts = 1;
        pending.valid = true;

        if (relay_start() != 0)
        {
            pending.valid = false;
        }
    }
}

GOLIOTH_OTA_MANIFEST_HANDLER(fw_manifest);

/*
 * The host's verdict on an image we relayed.
 *
 * This is the only place a rejection can be acted on. By the time it arrives
 * the device has called golioth_ota_mark_updating(), which tells the cloud to
 * stop offering the component, and the OTA manifest that would otherwise
 * re-offer it is delivered once per connection - so a device that waits for
 * another manifest after a rejection never retries. Re-arming the download from
 * here is what makes a retry possible at all.
 */
static void fw_apply_status(enum pouch_serial_fw_status status)
{
    if (status == POUCH_SERIAL_FW_STATUS_OK)
    {
        LOG_INF("Host installed %s; awaiting restart", pending.version);
        pending.valid = false;
        return;
    }

    LOG_WRN("Host rejected the image (status %d)", status);

    if (!pending.valid)
    {
        return;
    }

    if (pending.attempts >= FW_MAX_ATTEMPTS)
    {
        LOG_ERR("Giving up on %s after %d attempts", pending.version, pending.attempts);
        pending.valid = false;
        relaying = false;
        /* Leaving the component "updating" would stop the cloud sending any
         * component at all, not just this one.
         */
        golioth_ota_mark_idle(FW_COMPONENT);
        return;
    }

    /* Nothing is in flight after a verdict, but a relay abandoned mid-image
     * would still be active - clear it either way so begin() can restart.
     */
    pouch_serial_fw_abort();
    relaying = false;

    pending.attempts++;
    LOG_INF("Retrying %s (attempt %d/%d)", pending.version, pending.attempts, FW_MAX_ATTEMPTS);

    if (relay_start() != 0)
    {
        pending.valid = false;
        golioth_ota_mark_idle(FW_COMPONENT);
    }
}

static int fw_relay_init(void)
{
    pouch_serial_fw_status_callback_set(fw_apply_status);
    return 0;
}

SYS_INIT(fw_relay_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
