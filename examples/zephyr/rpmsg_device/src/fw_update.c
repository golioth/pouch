/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-mediated firmware update.
 *
 * This core has no flash of its own - the host loads it through remoteproc at
 * every boot - so it cannot apply an update itself. It is still the
 * authenticated Pouch endpoint, so it is the one the cloud offers updates to,
 * and it arranges for the host to install one.
 *
 * There are two ways to arrange that, and this file chooses between them per
 * attempt. Handing the host a signed URL is preferred: the host fetches the
 * image directly and no byte of it crosses the link. Relaying the image through
 * this core works everywhere, and is the fallback when a URL cannot be signed
 * or when the host would not accept one.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fw_update, LOG_LEVEL_INF);

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <string.h>

#include <golioth/ota.h>
#include <pouch/transport/serial/fw.h>

#include <app_version.h>

#include "fw_backend.h"

#define FW_COMPONENT CONFIG_EXAMPLE_FW_COMPONENT

/*
 * How many times to re-offer an image the host rejects before giving up. A
 * rejection is usually a corrupt transfer, which a retry fixes; a genuinely bad
 * image would otherwise retry every session forever.
 */
#define FW_MAX_ATTEMPTS 3

enum fw_mode
{
    FW_MODE_NONE,
    FW_MODE_SIGNED_URL,
    FW_MODE_RELAY,
};

static struct
{
    struct fw_pending image;
    enum fw_mode mode;
    int attempts;
    bool valid;
    /* Set when a signed-URL attempt came back rejected. The next attempt falls
     * back to the relay: the likely causes - signed URLs not enabled for the
     * project, or a certificate authority the cloud does not know - are not
     * things a second signature would fix.
     */
    bool url_rejected;
} update;

static enum fw_mode pick_mode(void)
{
#if defined(CONFIG_EXAMPLE_FW_SIGNED_URL)
    if (!update.url_rejected && fw_signed_url_ready())
    {
        return FW_MODE_SIGNED_URL;
    }
#endif

#if defined(CONFIG_EXAMPLE_FW_RELAY)
    return FW_MODE_RELAY;
#else
    return FW_MODE_NONE;
#endif
}

static int start_attempt(void)
{
    update.mode = pick_mode();

    switch (update.mode)
    {
#if defined(CONFIG_EXAMPLE_FW_SIGNED_URL)
        case FW_MODE_SIGNED_URL:
            LOG_INF("Handing the host a signed URL for %s", update.image.version);
            return fw_signed_url_start(&update.image);
#endif
#if defined(CONFIG_EXAMPLE_FW_RELAY)
        case FW_MODE_RELAY:
            LOG_INF("Relaying %s through this core", update.image.version);
            return fw_relay_start(&update.image);
#endif
        default:
            LOG_ERR("No way to deliver %s to the host", update.image.version);
            return -ENOTSUP;
    }
}

/* Stop whatever the current attempt left in flight. */
static void abort_attempt(void)
{
    switch (update.mode)
    {
#if defined(CONFIG_EXAMPLE_FW_SIGNED_URL)
        case FW_MODE_SIGNED_URL:
            fw_signed_url_abort();
            break;
#endif
#if defined(CONFIG_EXAMPLE_FW_RELAY)
        case FW_MODE_RELAY:
            fw_relay_abort();
            break;
#endif
        default:
            break;
    }

    update.mode = FW_MODE_NONE;
}

/* Runs on the downlink decrypt work queue. Only the relay ever sees data here:
 * a signed-URL handoff never marks the component for download, so the cloud
 * sends none.
 */
static void fw_receive(const void *data, size_t offset, size_t len, bool is_last)
{
#if defined(CONFIG_EXAMPLE_FW_RELAY)
    fw_relay_receive(data, offset, len, is_last);
#else
    ARG_UNUSED(data);
    ARG_UNUSED(offset);
    ARG_UNUSED(len);
    ARG_UNUSED(is_last);
#endif
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

        if (strlen(c->target) > POUCH_SERIAL_FW_MAX_NAME_LEN)
        {
            LOG_ERR("Target version %s is too long to deliver", c->target);
            continue;
        }

        strcpy(update.image.version, c->target);
        memcpy(update.image.hash, c->target_hash, sizeof(update.image.hash));
        update.image.size = c->size;
        update.attempts = 1;
        update.url_rejected = false;
        update.valid = true;

        if (start_attempt() != 0)
        {
            update.valid = false;
            update.mode = FW_MODE_NONE;
        }
    }
}

GOLIOTH_OTA_MANIFEST_HANDLER(fw_manifest);

/*
 * The host's verdict on an image we handed it.
 *
 * This is the only place a rejection can be acted on. By the time it arrives
 * the device has called golioth_ota_mark_updating(), which tells the cloud to
 * stop offering the component, and the OTA manifest that would otherwise
 * re-offer it is delivered once per connection - so a device that waits for
 * another manifest after a rejection never retries. Re-arming from here is what
 * makes a retry possible at all.
 */
static void fw_apply_status(enum pouch_serial_fw_status status)
{
    if (status == POUCH_SERIAL_FW_STATUS_OK)
    {
        LOG_INF("Host installed %s; awaiting restart", update.image.version);
        update.valid = false;
        update.mode = FW_MODE_NONE;
        return;
    }

    LOG_WRN("Host rejected the image (status %d)", status);

    if (!update.valid)
    {
        return;
    }

    if (update.mode == FW_MODE_SIGNED_URL)
    {
        /* Not something a fresh signature would fix, so the retry - if there is
         * one - goes through the relay instead.
         */
        LOG_WRN("Signed-URL delivery was refused; falling back to the relay");
        update.url_rejected = true;
    }

    if (update.attempts >= FW_MAX_ATTEMPTS)
    {
        LOG_ERR("Giving up on %s after %d attempts", update.image.version, update.attempts);
        abort_attempt();
        update.valid = false;
        /* Leaving the component "updating" would stop the cloud sending any
         * component at all, not just this one.
         */
        golioth_ota_mark_idle(FW_COMPONENT);
        return;
    }

    /* Nothing is in flight after a verdict, but an attempt abandoned mid-image
     * would still be active - clear it either way so the next can start.
     */
    abort_attempt();

    update.attempts++;
    LOG_INF("Retrying %s (attempt %d/%d)", update.image.version, update.attempts, FW_MAX_ATTEMPTS);

    if (start_attempt() != 0)
    {
        update.valid = false;
        update.mode = FW_MODE_NONE;
        golioth_ota_mark_idle(FW_COMPONENT);
    }
}

static int fw_update_init(void)
{
    pouch_serial_fw_status_callback_set(fw_apply_status);
    return 0;
}

SYS_INIT(fw_update_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
