/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <pouch/transport/rpmsg/device.h>
#include <pouch/transport/serial/device.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(pouch_rpmsg_device, CONFIG_POUCH_RPMSG_DEVICE_LOG_LEVEL);

/*
 * The rpmsg device transport is a physical adapter on top of the Pouch Serial
 * device core. rpmsg is reliable, ordered and message-oriented, so each rpmsg
 * message carries exactly one Pouch Serial frame (1-byte header + payload) and
 * no length prefix or segmentation is needed. Receiving a message feeds the
 * serial core directly; the serial core signals (via ready_cb) when it has a
 * frame to send, which we pull and transmit over the endpoint.
 */

#define POUCH_RPMSG_IPC_NODE DT_CHOSEN(golioth_pouch_rpmsg_ipc)

BUILD_ASSERT(DT_NODE_EXISTS(POUCH_RPMSG_IPC_NODE),
             "The chosen node golioth,pouch-rpmsg-ipc must reference an ipc_service instance");

/* Milliseconds to wait before retrying a send that failed for lack of TX buffers. */
#define POUCH_RPMSG_TX_RETRY_MS 2

enum flags
{
    FLAG_INITED,
    FLAG_BOUND,
};

static const struct device *const ipc_instance = DEVICE_DT_GET(POUCH_RPMSG_IPC_NODE);

static struct ipc_ept ept;
static atomic_t flags;

static struct k_work_delayable tx_work;

/* A single in-flight frame pulled from the serial core, held across send
 * retries so that a transient lack of TX buffers never drops a frame.
 */
static uint8_t tx_frame[CONFIG_POUCH_RPMSG_DEVICE_FRAME_SIZE];
static size_t tx_pending;

static void tx_process(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!atomic_test_bit(&flags, FLAG_BOUND))
    {
        return;
    }

    while (true)
    {
        if (tx_pending == 0)
        {
            tx_pending = pouch_serial_device_frame_get(tx_frame, sizeof(tx_frame));
            if (tx_pending == 0)
            {
                /* Nothing left to send. */
                return;
            }
        }

        int ret = ipc_service_send(&ept, tx_frame, tx_pending);
        if (ret == (int) tx_pending)
        {
            /* Frame sent; pull the next one. */
            tx_pending = 0;
            continue;
        }

        if (ret == -ENOMEM || ret == -EAGAIN || ret == -ENOBUFS || ret == 0)
        {
            /* No TX buffer available right now. Keep the pending frame and
             * retry shortly.
             */
            k_work_reschedule(&tx_work, K_MSEC(POUCH_RPMSG_TX_RETRY_MS));
            return;
        }

        LOG_ERR("Send failed (%d), dropping frame", ret);
        tx_pending = 0;
        return;
    }
}

static void kick_tx(void)
{
    k_work_reschedule(&tx_work, K_NO_WAIT);
}

/* Serial core -> adapter: a frame is available to send. */
static void serial_ready_cb(void)
{
    kick_tx();
}

/* rpmsg endpoint callbacks. */

static void ept_bound(void *priv)
{
    ARG_UNUSED(priv);
    LOG_DBG("Endpoint bound");
    atomic_set_bit(&flags, FLAG_BOUND);
    kick_tx();
}

static void ept_recv(const void *data, size_t len, void *priv)
{
    ARG_UNUSED(priv);

    if (len == 0)
    {
        return;
    }

    LOG_HEXDUMP_DBG(data, len, "RX");

    int err = pouch_serial_device_recv(data, len);
    if (err)
    {
        LOG_ERR("RX process failed: %d", err);
    }

    /* Receiving may have produced a response frame (e.g. an ACK); make sure the
     * TX path runs even if ready_cb was not invoked.
     */
    kick_tx();
}

static void ept_error(const char *message, void *priv)
{
    ARG_UNUSED(priv);
    LOG_ERR("Endpoint error: %s", message ? message : "(unknown)");
}

static const struct ipc_ept_cfg ept_cfg = {
    .name = CONFIG_POUCH_RPMSG_DEVICE_EPT_NAME,
    .cb =
        {
            .bound = ept_bound,
            .received = ept_recv,
            .error = ept_error,
        },
};

int pouch_rpmsg_device_init(void)
{
    if (atomic_test_and_set_bit(&flags, FLAG_INITED))
    {
        return 0;
    }

    k_work_init_delayable(&tx_work, tx_process);

    if (!device_is_ready(ipc_instance))
    {
        LOG_ERR("ipc_service instance not ready");
        return -ENODEV;
    }

    /* Bring up the serial core before the endpoint so that frames can be
     * delivered as soon as the endpoint binds.
     */
    pouch_serial_device_init(serial_ready_cb);

    int err = ipc_service_open_instance(ipc_instance);
    if (err < 0 && err != -EALREADY)
    {
        LOG_ERR("Failed to open ipc_service instance: %d", err);
        return err;
    }

    err = ipc_service_register_endpoint(ipc_instance, &ept, &ept_cfg);
    if (err < 0)
    {
        LOG_ERR("Failed to register endpoint: %d", err);
        return err;
    }

    LOG_DBG("rpmsg device transport ready");
    return 0;
}

bool pouch_rpmsg_device_is_bound(void)
{
    return atomic_test_bit(&flags, FLAG_BOUND);
}

SYS_INIT(pouch_rpmsg_device_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
