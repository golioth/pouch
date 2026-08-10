/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/ipm.h>

#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/io.h>
#include <resource_table.h>
#include <addr_translation.h>

#include <pouch/transport/serial/device.h>
#include <pouch/transport/serial/fw.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(pouch_rpmsg_rsc, CONFIG_POUCH_RPMSG_RSC_DEVICE_LOG_LEVEL);

/*
 * Pouch Serial device adapter for MPU+MCU parts where the Linux host drives
 * virtio-rpmsg through remoteproc and a resource table. This is the raw
 * OpenAMP twin of rpmsg_device.c (which fronts an ipc_service instance):
 * Zephyr's DT-instantiable ipc_service backends speak their own static-vring
 * layout that the Linux kernel's virtio_rpmsg_bus does not understand, so on
 * these systems the vdev must come from the resource table, exactly like the
 * openamp_rsc_table sample this platform bring-up is lifted from.
 *
 * rpmsg is reliable, ordered and message-oriented: each rpmsg message carries
 * exactly one Pouch Serial frame (1-byte header + payload).
 */

#if !DT_HAS_CHOSEN(zephyr_ipc_shm)
#error "The rpmsg (rsc table) adapter requires a zephyr,ipc_shm chosen node"
#endif

#define SHM_NODE       DT_CHOSEN(zephyr_ipc_shm)
#define SHM_START_ADDR DT_REG_ADDR(SHM_NODE)
#define SHM_SIZE       DT_REG_SIZE(SHM_NODE)

static const struct device *const ipm_handle = DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

static metal_phys_addr_t shm_physmap = SHM_START_ADDR;
static metal_phys_addr_t rsc_tab_physmap;

static struct metal_io_region shm_io_data;
static struct metal_io_region rsc_io_data;

static struct rpmsg_virtio_device rvdev;
static void *rsc_table;
static struct rpmsg_device *rpdev;
static struct rpmsg_endpoint pouch_ept;

static K_SEM_DEFINE(ipm_sem, 0, 1);
static K_SEM_DEFINE(tx_sem, 0, 1);
static K_SEM_DEFINE(ept_ready_sem, 0, 1);

static K_THREAD_STACK_DEFINE(mng_stack, CONFIG_POUCH_RPMSG_RSC_DEVICE_STACK_SIZE);
static K_THREAD_STACK_DEFINE(tx_stack, CONFIG_POUCH_RPMSG_RSC_DEVICE_STACK_SIZE);
static struct k_thread mng_thread;
static struct k_thread tx_thread;

static uint8_t tx_frame[CONFIG_POUCH_RPMSG_RSC_DEVICE_FRAME_SIZE];

static uint32_t rx_frames;
static uint32_t tx_frames;

static void platform_ipm_callback(const struct device *dev, void *context, uint32_t id,
                                  volatile void *data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(context);
    ARG_UNUSED(id);
    ARG_UNUSED(data);
    k_sem_give(&ipm_sem);
}

static int mailbox_notify(void *priv, uint32_t id)
{
    ARG_UNUSED(priv);
#if CONFIG_IPM_MAX_DATA_SIZE > 0
    ipm_send(ipm_handle, 0, id, &id, 4);
#else
    ipm_send(ipm_handle, 0, id, NULL, 0);
#endif
    return 0;
}

static int pouch_ept_cb(struct rpmsg_endpoint *ept, void *data, size_t len, uint32_t src,
                        void *priv)
{
    ARG_UNUSED(ept);
    ARG_UNUSED(src);
    ARG_UNUSED(priv);

    if (len == 0)
    {
        return RPMSG_SUCCESS;
    }

    rx_frames++;
    LOG_DBG("rx %u: %zu bytes, hdr 0x%02x", rx_frames, len, ((const uint8_t *) data)[0]);

    /* Stall while the firmware relay is backed up. Not returning from this
     * callback leaves the receive buffer unreturned, so the host's write(2)
     * blocks and the backpressure reaches the broker. Without it a fast link
     * delivers a whole component faster than the relay drains and the downlink
     * heap-allocates itself to death. The transmit thread keeps draining the
     * firmware channel meanwhile, which is what clears the pressure.
     */
    for (uint32_t waited = 0;
         pouch_serial_fw_pressure() && waited < CONFIG_POUCH_RPMSG_RSC_DEVICE_RX_STALL_MS;
         waited += 2)
    {
        k_sleep(K_MSEC(2));
    }

    int err = pouch_serial_device_recv(data, len);
    if (err)
    {
        LOG_ERR("RX process failed: %d", err);
    }

    /* Receiving may have produced a response frame (e.g. an ACK). */
    k_sem_give(&tx_sem);

    return RPMSG_SUCCESS;
}

static void pouch_ept_unbound(struct rpmsg_endpoint *ept)
{
    ARG_UNUSED(ept);
    LOG_WRN("Endpoint unbound");
}

/* Serial core -> adapter: a frame is available to send. */
static void serial_ready_cb(void)
{
    k_sem_give(&tx_sem);
}

static void ns_bind_cb(struct rpmsg_device *rdev, const char *name, uint32_t src)
{
    ARG_UNUSED(rdev);
    LOG_WRN("Unexpected NS announcement for %s (src %u)", name, src);
}

/* The Linux host may kick the MU the moment the core starts, and the MU's
 * interrupt-enable bits survive a remoteproc restart from a previous firmware,
 * so the mailbox ISR can run long before this transport's threads. Register
 * the IPM callback in the earliest init slot after the ipm device exists so a
 * stray kick finds a valid callback rather than a NULL pointer.
 */
static int ipm_early_init(void)
{
    if (!device_is_ready(ipm_handle))
    {
        LOG_ERR("IPM device not ready at early init");
        return -ENODEV;
    }

    ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);
    return 0;
}

SYS_INIT(ipm_early_init, POST_KERNEL, 99);

static int platform_init(void)
{
    struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
    int rsc_size;
    int status;

    status = metal_init(&metal_params);
    if (status)
    {
        LOG_ERR("metal_init failed: %d", status);
        return -EIO;
    }

    metal_io_init(&shm_io_data,
                  (void *) SHM_START_ADDR,
                  &shm_physmap,
                  SHM_SIZE,
                  -1,
                  0,
                  addr_translation_get_ops(shm_physmap));

    rsc_table_get(&rsc_table, &rsc_size);
    rsc_tab_physmap = (uintptr_t) rsc_table;
    metal_io_init(&rsc_io_data, rsc_table, &rsc_tab_physmap, rsc_size, -1, 0, NULL);

    if (!device_is_ready(ipm_handle))
    {
        LOG_ERR("IPM device not ready");
        return -ENODEV;
    }

    status = ipm_set_enabled(ipm_handle, 1);
    if (status)
    {
        LOG_ERR("ipm_set_enabled failed: %d", status);
        return -EIO;
    }

    return 0;
}

static struct rpmsg_device *create_rpmsg_vdev(void)
{
    struct fw_rsc_vdev_vring *vring_rsc;
    struct virtio_device *vdev;
    int ret;

    vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE,
                                    VDEV_ID,
                                    rsc_table_to_vdev(rsc_table),
                                    &rsc_io_data,
                                    NULL,
                                    mailbox_notify,
                                    NULL);
    if (!vdev)
    {
        LOG_ERR("failed to create vdev");
        return NULL;
    }

    /* Wait for the Linux virtio host to finish its rpmsg init. */
    rproc_virtio_wait_remote_ready(vdev);

    vring_rsc = rsc_table_get_vring0(rsc_table);
    ret = rproc_virtio_init_vring(vdev,
                                  0,
                                  vring_rsc->notifyid,
                                  (void *) vring_rsc->da,
                                  &rsc_io_data,
                                  vring_rsc->num,
                                  vring_rsc->align);
    if (ret)
    {
        goto failed;
    }

    vring_rsc = rsc_table_get_vring1(rsc_table);
    ret = rproc_virtio_init_vring(vdev,
                                  1,
                                  vring_rsc->notifyid,
                                  (void *) vring_rsc->da,
                                  &rsc_io_data,
                                  vring_rsc->num,
                                  vring_rsc->align);
    if (ret)
    {
        goto failed;
    }

    ret = rpmsg_init_vdev(&rvdev, vdev, ns_bind_cb, &shm_io_data, NULL);
    if (ret)
    {
        LOG_ERR("rpmsg_init_vdev failed: %d", ret);
        goto failed;
    }

    return rpmsg_virtio_get_rpmsg_device(&rvdev);

failed:
    rproc_virtio_remove_vdev(vdev);
    return NULL;
}

/* Push every frame the serial core has ready. This runs on its own thread so
 * the firmware channel keeps draining while the receive callback is stalled on
 * relay pressure; libmetal's mutex makes concurrent rpmsg access safe.
 */
static void drain_tx(void)
{
    while (true)
    {
        size_t len = pouch_serial_device_frame_get(tx_frame, sizeof(tx_frame));
        if (len == 0)
        {
            break;
        }

        /* Never use the blocking rpmsg_send(): it spins until a TX buffer
         * frees while this thread is the only one that can service the vrings,
         * so a host with no free buffers would never get them back. Give up
         * after a bounded wait instead.
         */
        int ret;
        uint32_t waited = 0;

        while ((ret = rpmsg_trysend(&pouch_ept, tx_frame, len)) == RPMSG_ERR_NO_BUFF
               && waited < CONFIG_POUCH_RPMSG_RSC_DEVICE_TX_WAIT_MS)
        {
            k_sleep(K_MSEC(1));
            waited++;
        }

        if (ret < 0)
        {
            LOG_ERR("rpmsg_trysend failed (%d) after %u ms, dropping frame", ret, waited);
        }
        else
        {
            tx_frames++;
            LOG_DBG("tx %u: %zu bytes, hdr 0x%02x", tx_frames, len, tx_frame[0]);
            if (waited > 0)
            {
                LOG_WRN("tx buffer starved %u ms (frame %u)", waited, tx_frames);
            }
        }
    }
}

static void mng_task(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    if (platform_init())
    {
        return;
    }

    rpdev = create_rpmsg_vdev();
    if (!rpdev)
    {
        LOG_ERR("Failed to create rpmsg virtio device");
        return;
    }

    int err = rpmsg_create_ept(&pouch_ept,
                               rpdev,
                               CONFIG_POUCH_RPMSG_RSC_DEVICE_EPT_NAME,
                               RPMSG_ADDR_ANY,
                               RPMSG_ADDR_ANY,
                               pouch_ept_cb,
                               pouch_ept_unbound);
    if (err)
    {
        LOG_ERR("rpmsg_create_ept failed: %d", err);
        return;
    }

    LOG_INF("rpmsg (rsc table) device transport ready");
    k_sem_give(&ept_ready_sem);
    k_sem_give(&tx_sem);

    while (true)
    {
        /* Poll rather than block indefinitely. The serial core's frame_get()
         * returns 0 when an endpoint reports POUCH_MORE_DATA with no bytes yet
         * ("wait for the next call", channel.c), but nothing schedules that
         * call: the uplink endpoint produces its data asynchronously on the
         * pouch work queue and never invokes bearer_ready(). Waiting only on
         * the mailbox would therefore stall a session forever in the window
         * between the broker's prompt and the first encrypted block.
         */
        k_sem_take(&ipm_sem, K_MSEC(CONFIG_POUCH_RPMSG_RSC_DEVICE_TX_POLL_MS));
        rproc_virtio_notified(rvdev.vdev, VRING1_ID);
    }
}

static void tx_task(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    k_sem_take(&ept_ready_sem, K_FOREVER);

    while (true)
    {
        /* Poll rather than block indefinitely. The serial core's frame_get()
         * returns 0 when an endpoint reports POUCH_MORE_DATA with no bytes yet
         * ("wait for the next call", channel.c), but nothing schedules that
         * call: the uplink endpoint produces its data asynchronously on the
         * pouch work queue and never invokes bearer_ready(). Waiting only on
         * the semaphore would therefore stall a session forever in the window
         * between the broker's prompt and the first encrypted block.
         */
        k_sem_take(&tx_sem, K_MSEC(CONFIG_POUCH_RPMSG_RSC_DEVICE_TX_POLL_MS));
        drain_tx();
    }
}

static int pouch_rpmsg_rsc_init(void)
{
    /* Bring up the serial core before the endpoint so frames can be delivered
     * as soon as the host binds.
     */
    pouch_serial_device_init(serial_ready_cb);

    k_thread_create(&mng_thread,
                    mng_stack,
                    K_THREAD_STACK_SIZEOF(mng_stack),
                    mng_task,
                    NULL,
                    NULL,
                    NULL,
                    CONFIG_POUCH_RPMSG_RSC_DEVICE_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&mng_thread, "pouch_rpmsg_mng");

    /* Above the management thread: the firmware channel has to keep draining
     * while the receive callback is stalled waiting for it.
     */
    k_thread_create(&tx_thread,
                    tx_stack,
                    K_THREAD_STACK_SIZEOF(tx_stack),
                    tx_task,
                    NULL,
                    NULL,
                    NULL,
                    CONFIG_POUCH_RPMSG_RSC_DEVICE_TX_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&tx_thread, "pouch_rpmsg_tx");


    return 0;
}

SYS_INIT(pouch_rpmsg_rsc_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
