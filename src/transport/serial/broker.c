/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <pouch/transport/serial/broker.h>
#include "packet.h"
#include "serial.h"
#include "gateway/types.h"
#include "transport/bearer.h"
#include "transport/endpoints/broker/endpoints.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <pouch/port.h>

POUCH_LOG_REGISTER(pouch_serial_broker, CONFIG_POUCH_SERIAL_LOG_LEVEL);

enum flags
{
    FLAG_INFO_READ,
    FLAG_SYNC,
    FLAG_UPLINK_DONE,
    FLAG_DOWNLINK_DONE,
};

struct pouch_serial_broker
{
    struct pouch_serial serial;
    const struct pouch_serial_broker_adapter *adapter;
    struct pouch_gateway_node_info node;
    pouch_atomic_t flags;
};

static void signal_adapter_ready(struct pouch_serial_broker *broker)
{
    if (broker->adapter->ready)
    {
        broker->adapter->ready(broker);
    }
}

static void channel_ready(struct pouch_serial_broker *broker, enum pouch_serial_channel_id ch)
{
    pouch_serial_ch_ready(&broker->serial.channels[ch]);
}

static void next(struct pouch_serial_broker *broker)
{
    if (!pouch_atomic_test_and_set_bit(&broker->flags, FLAG_INFO_READ))
    {
        channel_ready(broker, POUCH_SERIAL_CH_INFO);
        signal_adapter_ready(broker);
        return;
    }

    if (!broker->node.server_cert_provisioned)
    {
        channel_ready(broker, POUCH_SERIAL_CH_SERVER_CERT);
        signal_adapter_ready(broker);
        return;
    }

    if (!broker->node.device_cert_provisioned)
    {
        channel_ready(broker, POUCH_SERIAL_CH_DEVICE_CERT);
        signal_adapter_ready(broker);
        return;
    }

    if (!pouch_atomic_test_and_set_bit(&broker->flags, FLAG_SYNC))
    {
        channel_ready(broker, POUCH_SERIAL_CH_UPLINK);
        channel_ready(broker, POUCH_SERIAL_CH_DOWNLINK);
        signal_adapter_ready(broker);
        return;
    }

    if (pouch_atomic_test_bit(&broker->flags, FLAG_DOWNLINK_DONE)
        && pouch_atomic_test_bit(&broker->flags, FLAG_UPLINK_DONE))
    {
        if (broker->adapter->end)
        {
            broker->adapter->end(broker, true);
        }
    }
}

static void serial_ready(struct pouch_serial *s)
{
    struct pouch_serial_broker *broker = CONTAINER_OF(s, struct pouch_serial_broker, serial);
    signal_adapter_ready(broker);
}

static void channel_closed(struct pouch_serial *s, enum pouch_serial_channel_id ch, bool success)
{
    struct pouch_serial_broker *broker = CONTAINER_OF(s, struct pouch_serial_broker, serial);
    if (!success)
    {
        pouch_atomic_clear(&broker->flags);
        if (broker->adapter->end)
        {
            broker->adapter->end(broker, false);
        }
        return;
    }

    POUCH_LOG_DBG("Channel %u completed successfully", ch);

    if (ch == POUCH_SERIAL_CH_UPLINK)
    {
        pouch_atomic_set_bit(&broker->flags, FLAG_UPLINK_DONE);
    }
    else if (ch == POUCH_SERIAL_CH_DOWNLINK)
    {
        pouch_atomic_set_bit(&broker->flags, FLAG_DOWNLINK_DONE);
    }

    next(broker);
}

struct pouch_serial_broker *pouch_serial_broker_create(
    const struct pouch_serial_broker_adapter *adapter)
{
    if (adapter == NULL)
    {
        return NULL;
    }

    struct pouch_serial_broker *broker = malloc(sizeof(*broker));
    if (broker == NULL)
    {
        return NULL;
    }

    memset(broker, 0, sizeof(*broker));
    broker->adapter = adapter;

    broker->serial.channels[POUCH_SERIAL_CH_INFO].endpoint = &broker_endpoint_info;
    broker->serial.channels[POUCH_SERIAL_CH_DEVICE_CERT].endpoint = &broker_endpoint_device_cert;
    broker->serial.channels[POUCH_SERIAL_CH_SERVER_CERT].endpoint = &broker_endpoint_server_cert;
    broker->serial.channels[POUCH_SERIAL_CH_DOWNLINK].endpoint = &broker_endpoint_downlink;
    broker->serial.channels[POUCH_SERIAL_CH_UPLINK].endpoint = &broker_endpoint_uplink;

    for (enum pouch_serial_channel_id i = 0; i < POUCH_SERIAL_CHANNEL_COUNT; i++)
    {
        broker->serial.channels[i].bearer.ctx = &broker->node;
    }

    pouch_serial_init(&broker->serial, serial_ready, channel_closed);

    return broker;
}

void pouch_serial_broker_destroy(struct pouch_serial_broker *broker)
{
    free(broker);
}

int pouch_serial_broker_start(struct pouch_serial_broker *broker)
{
    if (broker == NULL)
    {
        return -EINVAL;
    }

    POUCH_LOG_DBG("Starting broker %p", broker);
    pouch_atomic_clear_bit(&broker->flags, FLAG_SYNC);
    pouch_atomic_clear_bit(&broker->flags, FLAG_DOWNLINK_DONE);
    pouch_atomic_clear_bit(&broker->flags, FLAG_UPLINK_DONE);

    next(broker);
    return 0;
}

int pouch_serial_broker_recv(struct pouch_serial_broker *broker, const void *frame, size_t len)
{
    if (broker == NULL || frame == NULL || len == 0)
    {
        return -EINVAL;
    }

    int err = pouch_serial_recv(&broker->serial, frame, len);
    if (err)
    {
        POUCH_LOG_ERR("Failed to process received frame (%d)", err);
        return err;
    }

    return 0;
}

size_t pouch_serial_broker_frame_get(struct pouch_serial_broker *broker,
                                     uint8_t *buf,
                                     size_t maxlen)
{
    if (broker == NULL || buf == NULL)
    {
        return 0;
    }

    return pouch_serial_frame_get(&broker->serial, buf, maxlen);
}

void pouch_serial_broker_notify(struct pouch_serial_broker *broker)
{
    if (broker == NULL)
    {
        return;
    }

    pouch_serial_ch_ready(&broker->serial.channels[POUCH_SERIAL_CH_UPLINK]);
}

const struct pouch_serial_broker_adapter *pouch_serial_broker_adapter_get(
    const struct pouch_serial_broker *broker)
{
    if (broker == NULL)
    {
        return NULL;
    }

    return broker->adapter;
}
