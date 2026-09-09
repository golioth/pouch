/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <pouch/port.h>
#include <pouch/transport/serial/fw.h>
#include <pouch/types.h>

#include "endpoints.h"
#include "fw_internal.h"

POUCH_LOG_REGISTER(pouch_fw_status, CONFIG_POUCH_COMMON_LOG_LEVEL);

/*
 * Device-side apply verdict.
 *
 * The host is the only party that knows what became of an image it was handed,
 * and this is the channel it says so on. It is shared by every mechanism that
 * hands an image over - the relay that streams the bytes through this core, and
 * the signed URL that has the host fetch them itself - so it lives apart from
 * either, and a device that enables only one of them still pays for only one.
 */

static struct
{
    pouch_mutex_t lock;
    bool inited;
    bool valid;
    enum pouch_serial_fw_status status;
} verdict;

static pouch_serial_fw_status_cb_t status_cb;

static void status_init_once(void)
{
    if (!verdict.inited)
    {
        pouch_mutex_init(&verdict.lock);
        verdict.inited = true;
    }
}

void pouch_serial_fw_status_reset(void)
{
    status_init_once();
    pouch_mutex_lock(&verdict.lock, POUCH_FOREVER);
    verdict.valid = false;
    pouch_mutex_unlock(&verdict.lock);
}

bool pouch_serial_fw_status_get(enum pouch_serial_fw_status *status)
{
    status_init_once();
    pouch_mutex_lock(&verdict.lock, POUCH_FOREVER);
    bool valid = verdict.valid;
    if (valid && status)
    {
        *status = verdict.status;
    }
    pouch_mutex_unlock(&verdict.lock);
    return valid;
}

void pouch_serial_fw_status_callback_set(pouch_serial_fw_status_cb_t cb)
{
    status_cb = cb;
}

/* --- serial endpoint: apply status (broker -> device) --- */

static int fw_status_start(struct pouch_bearer *bearer)
{
    status_init_once();
    return 0;
}

static int fw_status_recv(struct pouch_bearer *bearer, const void *buf, size_t len)
{
    const uint8_t *in = buf;

    if (len == 0)
    {
        return 0;
    }

    if (in[0] > POUCH_SERIAL_FW_STATUS_ERROR)
    {
        POUCH_LOG_WRN("Unknown firmware apply status %u from broker", in[0]);
        return -EINVAL;
    }

    enum pouch_serial_fw_status status = (enum pouch_serial_fw_status) in[0];

    status_init_once();
    pouch_mutex_lock(&verdict.lock, POUCH_FOREVER);
    verdict.status = status;
    verdict.valid = true;
    pouch_mutex_unlock(&verdict.lock);

    POUCH_LOG_INF("Firmware apply status from broker: %u", in[0]);

    /* Called with the lock released: the handler re-arms a rejected update,
     * which comes straight back into the module that announced it.
     */
    if (status_cb)
    {
        status_cb(status);
    }

    return 0;
}

const struct pouch_endpoint pouch_device_endpoint_fw_status = {
    .start = fw_status_start,
    .recv = fw_status_recv,
};
