/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pouch device example that syncs over the serial transport's UART device
 * adapter. On native_sim the link is a host pty (native-tty UART) that a broker
 * process attaches to; on a real MPU+MCU part the same device code runs over
 * the rpmsg adapter instead. The transport adapter initializes itself at boot,
 * so the application only initializes Pouch (with its device credentials for
 * end-to-end encryption) and registers its handlers.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rpmsg_device, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include <errno.h>

#include <pouch/pouch.h>
#include <pouch/uplink.h>

#include "credentials.h"

/* Called by Pouch each time a sync collects uplink data. */
static void report_telemetry(void)
{
    static uint32_t counter;
    char payload[64];

    int len = snprintk(payload,
                       sizeof(payload),
                       "{\"uptime\":%lld,\"counter\":%u}",
                       k_uptime_get() / MSEC_PER_SEC,
                       counter++);

    pouch_uplink_entry_write(".s/telemetry", POUCH_CONTENT_TYPE_JSON, payload, len, POUCH_FOREVER);
}
POUCH_UPLINK_HANDLER(report_telemetry);

int main(void)
{
    struct pouch_config config = {0};

    int err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return err;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return -EIO;
    }

    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return err;
    }

    LOG_INF("Pouch serial/rpmsg device ready");

    return 0;
}
