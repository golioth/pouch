/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <pouch/gateway/cloud.h>
#include <pouch/port.h>

POUCH_LOG_REGISTER(gw_cloud, CONFIG_POUCH_GATEWAY_LOG_LEVEL);

static const struct pouch_gateway_cloud_transport *current;

void pouch_gateway_cloud_transport_register(const struct pouch_gateway_cloud_transport *transport)
{
    current = transport;

    if (transport == NULL)
    {
        POUCH_LOG_INF("Cloud transport unregistered");
    }
    else
    {
        POUCH_LOG_INF("Cloud transport registered");
    }
}

const struct pouch_gateway_cloud_transport *pouch_gateway_cloud_transport_get(void)
{
    return current;
}

int pouch_gateway_cloud_ensure_ready(void)
{
    if (current == NULL || current->ensure_ready == NULL)
    {
        return 0;
    }

    return current->ensure_ready();
}

int pouch_gateway_cloud_forward_pouch(pouch_gateway_cloud_upload_chunk_cb_t chunk_cb,
                                      void *chunk_arg,
                                      pouch_gateway_cloud_block2_cb_t resp_cb,
                                      void *resp_arg)
{
    if (current == NULL || current->forward_pouch == NULL)
    {
        POUCH_LOG_ERR("No cloud transport registered");
        return -ENODEV;
    }

    return current->forward_pouch(chunk_cb, chunk_arg, resp_cb, resp_arg);
}

int pouch_gateway_cloud_upload_device_cert(const uint8_t *cert, size_t len)
{
    if (current == NULL || current->upload_device_cert == NULL)
    {
        POUCH_LOG_ERR("No cloud transport registered");
        return -ENODEV;
    }

    return current->upload_device_cert(cert, len);
}
