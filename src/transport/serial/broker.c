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
#include <stdlib.h>

#include <pouch/port.h>

#define NO_CHANNEL 0xff

POUCH_LOG_REGISTER(pouch_serial_broker, CONFIG_POUCH_SERIAL_LOG_LEVEL);

enum flags
{
    /** The broker has been started, and hasn't failed since. */
    FLAG_RUNNING,
    /** All channels are being closed, and channel closures shouldn't be acted on. */
    FLAG_ABORTING,
    FLAG_INFO_READ,
    FLAG_SYNC,
    FLAG_UPLINK_DONE,
    FLAG_DOWNLINK_DONE,
    FLAG_NOTIFIED,
};

struct pouch_serial_broker
{
    struct pouch_serial serial;
    const struct pouch_serial_broker_adapter *adapter;
    struct pouch_gateway_node_info node;
    pouch_atomic_t flags;
    uint8_t active_channel;
    /** Number of retries since the device last sent a valid frame. */
    unsigned int retries;
};

static void signal_adapter_ready(struct pouch_serial_broker *broker)
{
    POUCH_LOG_DBG("signal_adapter_ready: calling adapter->ready (broker=%p)", broker);
    if (broker->adapter->ready)
    {
        broker->adapter->ready(broker);
    }
}

/*
 * Close every open channel, telling the device to abandon its end of each transfer.
 */
static void close_all_channels(struct pouch_serial_broker *broker)
{
    pouch_atomic_set_bit(&broker->flags, FLAG_ABORTING);
    for (enum pouch_serial_channel_id i = 0; i < POUCH_SERIAL_CHANNEL_COUNT; i++)
    {
        pouch_serial_ch_close(&broker->serial.channels[i], false);
    }
    pouch_atomic_clear_bit(&broker->flags, FLAG_ABORTING);

    broker->active_channel = NO_CHANNEL;
}

/*
 * Abort the exchange after a failure. The broker stays idle until the adapter restarts it with
 * pouch_serial_broker_start().
 */
static void abort_exchange(struct pouch_serial_broker *broker)
{
    if (!pouch_atomic_test_and_clear_bit(&broker->flags, FLAG_RUNNING))
    {
        // Already aborted
        return;
    }

    POUCH_LOG_WRN("Aborting exchange");
    close_all_channels(broker);
    pouch_atomic_clear(&broker->flags);

    // The error frames from closing the channels need to go out
    signal_adapter_ready(broker);

    if (broker->adapter->end)
    {
        broker->adapter->end(broker, false);
    }
}

static int open_channel(struct pouch_serial_broker *broker, enum pouch_serial_channel_id ch)
{
    int err = pouch_serial_ch_open(&broker->serial.channels[ch]);
    if (err)
    {
        POUCH_LOG_ERR("Failed to open channel %u: %d", ch, err);
        abort_exchange(broker);
        return err;
    }

    broker->active_channel = ch;
    return 0;
}

static void next(struct pouch_serial_broker *broker)
{
    if (!pouch_atomic_test_bit(&broker->flags, FLAG_RUNNING))
    {
        return;
    }

    if (!pouch_atomic_test_and_set_bit(&broker->flags, FLAG_INFO_READ))
    {
        POUCH_LOG_DBG("next: starting INFO channel");
        if (open_channel(broker, POUCH_SERIAL_CH_INFO) == 0)
        {
            signal_adapter_ready(broker);
        }
        return;
    }

    if (!broker->node.server_cert_provisioned)
    {
        POUCH_LOG_DBG("next: starting SERVER_CERT channel");
        if (open_channel(broker, POUCH_SERIAL_CH_SERVER_CERT) == 0)
        {
            signal_adapter_ready(broker);
        }
        return;
    }

    if (!broker->node.device_cert_provisioned)
    {
        POUCH_LOG_DBG("next: starting DEVICE_CERT channel");
        if (open_channel(broker, POUCH_SERIAL_CH_DEVICE_CERT) == 0)
        {
            signal_adapter_ready(broker);
        }
        return;
    }

    if (!pouch_atomic_test_and_set_bit(&broker->flags, FLAG_SYNC))
    {
        POUCH_LOG_DBG("next: starting UPLINK + DOWNLINK channels");
        // The sync is starting, so any pending notification has been served
        pouch_atomic_clear_bit(&broker->flags, FLAG_NOTIFIED);
        if (open_channel(broker, POUCH_SERIAL_CH_DOWNLINK) == 0
            && open_channel(broker, POUCH_SERIAL_CH_UPLINK) == 0)
        {
            signal_adapter_ready(broker);
        }
        return;
    }

    if (pouch_atomic_test_bit(&broker->flags, FLAG_DOWNLINK_DONE)
        && pouch_atomic_test_bit(&broker->flags, FLAG_UPLINK_DONE))
    {
        POUCH_LOG_DBG("next: both UPLINK and DOWNLINK done, ending exchange");
        broker->active_channel = NO_CHANNEL;

        // link done, prepare for the next round:
        pouch_atomic_clear_bit(&broker->flags, FLAG_SYNC);
        pouch_atomic_clear_bit(&broker->flags, FLAG_DOWNLINK_DONE);
        pouch_atomic_clear_bit(&broker->flags, FLAG_UPLINK_DONE);

        if (broker->adapter->end)
        {
            broker->adapter->end(broker, true);
        }

        // The device may have asked for another sync while this one was in progress
        if (pouch_atomic_test_bit(&broker->flags, FLAG_NOTIFIED))
        {
            next(broker);
        }
        return;
    }

    POUCH_LOG_DBG("next: waiting (uplink_done=%d, downlink_done=%d)",
                  pouch_atomic_test_bit(&broker->flags, FLAG_UPLINK_DONE),
                  pouch_atomic_test_bit(&broker->flags, FLAG_DOWNLINK_DONE));
}

/*
 * Nudge the state machine if the device has asked for a sync since the last one started. A
 * notification that arrives during a sync is kept until that sync has ended.
 */
static void handle_notification(struct pouch_serial_broker *broker)
{
    if (pouch_atomic_test_bit(&broker->flags, FLAG_NOTIFIED)
        && !pouch_atomic_test_bit(&broker->flags, FLAG_SYNC))
    {
        next(broker);
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
    POUCH_LOG_DBG("channel_closed: ch=%u success=%d", ch, success);
    if (pouch_atomic_test_bit(&broker->flags, FLAG_ABORTING))
    {
        return;
    }

    if (broker->active_channel == ch)
    {
        broker->active_channel = NO_CHANNEL;
    }

    if (!success)
    {
        POUCH_LOG_WRN("channel %u failed", ch);
        abort_exchange(broker);
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

/*
 * Retry every channel that's waiting for a response, and abort the exchange if the device keeps
 * failing to respond. Waiting for the first fragment of a pulled transfer doesn't count, as the
 * device might not have any data yet.
 */
static void retry(struct pouch_serial_broker *broker)
{
    bool counted = false;
    for (enum pouch_serial_channel_id i = 0; i < POUCH_SERIAL_CHANNEL_COUNT; i++)
    {
        struct pouch_serial_channel *ch = &broker->serial.channels[i];
        bool awaiting_first = pouch_serial_ch_is_awaiting_first(ch);
        if (pouch_serial_ch_retry(ch) && !awaiting_first)
        {
            counted = true;
        }
    }

    if (counted && ++broker->retries > CONFIG_POUCH_SERIAL_BROKER_MAX_RETRIES)
    {
        POUCH_LOG_ERR("No response after %u retries", CONFIG_POUCH_SERIAL_BROKER_MAX_RETRIES);
        abort_exchange(broker);
    }
}

void pouch_serial_broker_start(struct pouch_serial_broker *broker)
{
    POUCH_LOG_DBG("Starting broker %p", broker);

    // Restarting an exchange in progress abandons it
    close_all_channels(broker);
    pouch_atomic_clear(&broker->flags);
    broker->retries = 0;
    pouch_atomic_set_bit(&broker->flags, FLAG_RUNNING);

    next(broker);
}

int pouch_serial_broker_recv(struct pouch_serial_broker *broker, const void *frame, size_t len)
{
    if (broker == NULL || frame == NULL || len == 0)
    {
        return -EINVAL;
    }

    handle_notification(broker);

    POUCH_LOG_DBG("pouch_serial_broker_recv: len=%zu", len);
    int err = pouch_serial_recv(&broker->serial, frame, len);
    if (err == -EBADMSG)
    {
        // The frame was broken on the way, so ask for it again
        POUCH_LOG_WRN("Received a malformed frame, retrying");
        retry(broker);
        return err;
    }

    if (err)
    {
        /* Transfers are stop-and-wait, so a frame that can't be processed leaves the exchange
         * waiting for a response that'll never come.
         */
        POUCH_LOG_ERR("Failed to process received frame (%d)", err);
        abort_exchange(broker);
        return err;
    }

    broker->retries = 0;
    return 0;
}

void pouch_serial_broker_timeout(struct pouch_serial_broker *broker)
{
    if (broker == NULL || !pouch_atomic_test_bit(&broker->flags, FLAG_RUNNING))
    {
        return;
    }

    POUCH_LOG_WRN("No response from the device, retrying");
    retry(broker);
}

bool pouch_serial_broker_is_waiting(struct pouch_serial_broker *broker)
{
    if (broker == NULL)
    {
        return false;
    }

    for (enum pouch_serial_channel_id i = 0; i < POUCH_SERIAL_CHANNEL_COUNT; i++)
    {
        if (pouch_serial_ch_is_waiting(&broker->serial.channels[i]))
        {
            return true;
        }
    }

    return false;
}

size_t pouch_serial_broker_frame_get(struct pouch_serial_broker *broker,
                                     uint8_t *buf,
                                     size_t maxlen)
{
    if (broker == NULL || buf == NULL)
    {
        return 0;
    }

    handle_notification(broker);

    return pouch_serial_frame_get(&broker->serial, buf, maxlen);
}

void pouch_serial_broker_notify(struct pouch_serial_broker *broker)
{
    if (broker == NULL)
    {
        return;
    }

    /* We want to be able to call this without invoking pouch behavior, so that this function can
     * be called from an ISR context. If we're idling, we'll just set a flag indicating that the
     * state machine should be nudged next time a frame is requested. During an exchange, there's
     * nothing to do: the device holds on to our requests until it has data, and lost frames are
     * recovered by pouch_serial_broker_timeout().
     */
    if (broker->active_channel == NO_CHANNEL)
    {
        pouch_atomic_set_bit(&broker->flags, FLAG_NOTIFIED);
    }
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
