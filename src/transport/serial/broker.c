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

#define NO_CHANNEL 0xff

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
    uint8_t active_channel;
};

static void signal_adapter_ready(struct pouch_serial_broker *broker)
{
    POUCH_LOG_DBG("signal_adapter_ready: calling adapter->ready (broker=%p)", broker);
    if (broker->adapter->ready)
    {
        broker->adapter->ready(broker);
    }
}

static void open_channel(struct pouch_serial_broker *broker, enum pouch_serial_channel_id ch)
{
    int err = pouch_serial_ch_open(&broker->serial.channels[ch]);
    if (err)
    {
        POUCH_LOG_ERR("Failed to open channel %u: %d", ch, err);
        if (broker->adapter->end)
        {
            broker->adapter->end(broker, false);
        }
        return;
    }

    broker->active_channel = ch;
}

static void next(struct pouch_serial_broker *broker)
{
    if (!pouch_atomic_test_and_set_bit(&broker->flags, FLAG_INFO_READ))
    {
        POUCH_LOG_DBG("next: starting INFO channel");
        open_channel(broker, POUCH_SERIAL_CH_INFO);
        signal_adapter_ready(broker);
        return;
    }

    if (!broker->node.server_cert_provisioned)
    {
        POUCH_LOG_DBG("next: starting SERVER_CERT channel");
        open_channel(broker, POUCH_SERIAL_CH_SERVER_CERT);
        signal_adapter_ready(broker);
        return;
    }

    if (!broker->node.device_cert_provisioned)
    {
        POUCH_LOG_DBG("next: starting DEVICE_CERT channel");
        open_channel(broker, POUCH_SERIAL_CH_DEVICE_CERT);
        signal_adapter_ready(broker);
        return;
    }

    if (!pouch_atomic_test_and_set_bit(&broker->flags, FLAG_SYNC))
    {
        POUCH_LOG_DBG("next: starting UPLINK + DOWNLINK channels");
        open_channel(broker, POUCH_SERIAL_CH_DOWNLINK);
        open_channel(broker, POUCH_SERIAL_CH_UPLINK);
        signal_adapter_ready(broker);
        return;
    }

    if (pouch_atomic_test_bit(&broker->flags, FLAG_DOWNLINK_DONE)
        && pouch_atomic_test_bit(&broker->flags, FLAG_UPLINK_DONE))
    {
        POUCH_LOG_DBG("next: both UPLINK and DOWNLINK done, ending exchange");
        broker->active_channel = NO_CHANNEL;
        if (broker->adapter->end)
        {
            broker->adapter->end(broker, true);
        }

        // start over:
        pouch_atomic_clear_bit(&broker->flags, FLAG_SYNC);
        pouch_atomic_clear_bit(&broker->flags, FLAG_DOWNLINK_DONE);
        pouch_atomic_clear_bit(&broker->flags, FLAG_UPLINK_DONE);
        next(broker);
        return;
    }

    POUCH_LOG_DBG("next: waiting (uplink_done=%d, downlink_done=%d)",
                  pouch_atomic_test_bit(&broker->flags, FLAG_UPLINK_DONE),
                  pouch_atomic_test_bit(&broker->flags, FLAG_DOWNLINK_DONE));
}

static void serial_ready(struct pouch_serial *s)
{
    struct pouch_serial_broker *broker = CONTAINER_OF(s, struct pouch_serial_broker, serial);
    if (broker->active_channel != NO_CHANNEL)
    {
        pouch_serial_ch_ready(&broker->serial.channels[broker->active_channel]);
    }

    signal_adapter_ready(broker);
}

static void channel_closed(struct pouch_serial *s, enum pouch_serial_channel_id ch, bool success)
{
    struct pouch_serial_broker *broker = CONTAINER_OF(s, struct pouch_serial_broker, serial);
    POUCH_LOG_DBG("channel_closed: ch=%u success=%d", ch, success);
    if (broker->active_channel == ch)
    {
        broker->active_channel = NO_CHANNEL;
    }

    if (!success)
    {
        POUCH_LOG_WRN("channel %u failed, resetting flags", ch);
        pouch_atomic_clear(&broker->flags);
        if (broker->adapter->end)
        {
            broker->adapter->end(broker, false);
        }

        // start over:
        pouch_serial_broker_start(broker);
        return;
    }

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
    broker->active_channel = NO_CHANNEL;

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

void pouch_serial_broker_start(struct pouch_serial_broker *broker)
{
    POUCH_LOG_DBG("Starting broker %p", broker);
    pouch_atomic_clear_bit(&broker->flags, FLAG_INFO_READ);
    pouch_atomic_clear_bit(&broker->flags, FLAG_SYNC);
    pouch_atomic_clear_bit(&broker->flags, FLAG_DOWNLINK_DONE);
    pouch_atomic_clear_bit(&broker->flags, FLAG_UPLINK_DONE);
    broker->active_channel = NO_CHANNEL;

    next(broker);
}

int pouch_serial_broker_recv(struct pouch_serial_broker *broker, const void *frame, size_t len)
{
    if (broker == NULL || frame == NULL || len == 0)
    {
        return -EINVAL;
    }

    POUCH_LOG_DBG("pouch_serial_broker_recv: len=%zu", len);
    int err = pouch_serial_recv(&broker->serial, frame, len);
    if (err)
    {
        POUCH_LOG_ERR("Failed to process received frame (%d)", err);
        // Start from the top again:
        pouch_serial_broker_start(broker);
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
    if (broker == NULL || broker->active_channel == NO_CHANNEL)
    {
        return;
    }

    pouch_serial_ch_ready(&broker->serial.channels[broker->active_channel]);
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
