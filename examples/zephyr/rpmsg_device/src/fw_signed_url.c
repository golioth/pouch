/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Signed-URL backend: sign a URL for this core's own artifact and hand it to
 * the host, which fetches the image itself. No image byte crosses the link and
 * nothing is buffered here.
 *
 * The signature is what makes this safe to hand over: it is made with the
 * device key, which never leaves this core, and it is valid only for one
 * artifact and only for a short window. The host gains the ability to fetch
 * that one file, and nothing else.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(fw_signed_url, LOG_LEVEL_INF);

#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <stdio.h>
#include <time.h>

#include <golioth/ota.h>
#include <pouch/transport/serial/fw_url.h>
#include <signy/signy.h>

#include "credentials.h"
#include "fw_backend.h"

#define FW_COMPONENT CONFIG_EXAMPLE_FW_COMPONENT

/* Base plus "package@version": both names are bounded by the wire format. */
#define URL_MAX (sizeof(CONFIG_EXAMPLE_SIGNED_URL_BASE) + 2 * POUCH_SERIAL_FW_MAX_NAME_LEN + 2)

static bool signy_ready;

bool fw_signed_url_ready(void)
{
    /* A signature carries the window it is valid for, so an unset clock
     * produces one the cloud will refuse. The host reports its own time on the
     * time channel, and only a host that implements this handoff does - so this
     * is also the test for whether there is anyone to hand a URL to.
     */
    return signy_ready && pouch_serial_time_synced();
}

int fw_signed_url_start(const struct fw_pending *pending)
{
    char url[URL_MAX];

    int n = snprintk(url,
                     sizeof(url),
                     "%s%s@%s",
                     CONFIG_EXAMPLE_SIGNED_URL_BASE,
                     FW_COMPONENT,
                     pending->version);
    if (n < 0 || n >= (int) sizeof(url))
    {
        LOG_ERR("Artifact URL does not fit");
        return -ENOMEM;
    }

    int err = pouch_serial_fw_url_begin(FW_COMPONENT,
                                        pending->version,
                                        (uint32_t) pending->size,
                                        pending->hash,
                                        url);
    if (err)
    {
        LOG_ERR("Failed to announce the artifact: %d", err);
        return err;
    }

    LOG_INF("Announced %s for the host to fetch", url);

    /* The image never comes through Pouch, so the component is not marked for
     * download. It is marked updating: the cloud has to stop offering it while
     * the host works, and the result comes back as an apply verdict.
     */
    golioth_ota_mark_updating(FW_COMPONENT);
    return 0;
}

void fw_signed_url_abort(void)
{
    pouch_serial_fw_url_abort();
}

static void set_clock(int64_t unix_seconds)
{
    struct timespec ts = {
        .tv_sec = (time_t) unix_seconds,
        .tv_nsec = 0,
    };

    if (clock_settime(CLOCK_REALTIME, &ts) != 0)
    {
        LOG_ERR("Failed to set the clock from the host");
        return;
    }

    LOG_DBG("Clock set from the host: %lld", (long long) unix_seconds);
}

static int fw_signed_url_init(void)
{
    struct pouch_cert cert;

    pouch_serial_time_callback_set(set_clock);

    psa_key_id_t key = load_signing_key();
    if (key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("No signing key; signed-URL updates are unavailable");
        return 0;
    }

    if (load_certificate(&cert) != 0)
    {
        LOG_ERR("No device certificate; signed-URL updates are unavailable");
        return 0;
    }

    int err = signy_init(key, cert.buffer, cert.size);
    if (err != 0)
    {
        /* -EINVAL here is most often the encoded certificate overflowing
         * CONFIG_SIGNY_MAX_CERT_SIZE, which the default does for a real
         * Golioth device certificate.
         */
        LOG_ERR("Failed to initialize signy: %d", err);
        return 0;
    }

    pouch_serial_fw_url_signer_set(signy_sign_url);
    signy_ready = true;

    LOG_INF("Signed-URL updates available once the host reports its time");
    return 0;
}

SYS_INIT(fw_signed_url_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
