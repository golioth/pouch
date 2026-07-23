/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pouch device example that syncs over the serial transport's UART device
 * adapter. On native_sim the link is a host pty (native-tty UART) that a broker
 * process attaches to; on a real MPU+MCU part the same device code runs over
 * the rpmsg adapter instead. The transport adapter initializes itself at boot,
 * so the application only initializes Pouch and registers its handlers.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rpmsg_device, LOG_LEVEL_INF);

#include <zephyr/kernel.h>

#include <pouch/pouch.h>
#include <pouch/uplink.h>

#include <golioth/settings_callbacks.h>

#define DEVICE_ID "rpmsg-native-sim-device"

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

static int led_setting_cb(bool new_value)
{
    LOG_INF("Received LED setting: %d", (int) new_value);

    return 0;
}
GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

int main(void)
{
    const struct pouch_config config = {
        .device_id = DEVICE_ID,
    };

    int err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return err;
    }

    LOG_INF("Pouch serial/rpmsg device ready (id: %s)", DEVICE_ID);

    return 0;
}
