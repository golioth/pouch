/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include "protocol.h"
#include "channels.h"
#include "transport/sar/sender.h"
#include "transport/sar/receiver.h"
#include "transport/endpoints/broker/endpoints.h"
#include "gateway/types.h"

static struct channel channels[SERIAL_CHANNELS] = {
    [SERIAL_CH_INFO] = CHANNEL_RECV(&broker_endpoint_info),
    [SERIAL_CH_DEVICE_CERT] = CHANNEL_RECV(&broker_endpoint_device_cert),
    [SERIAL_CH_SERVER_CERT] = CHANNEL_SENDER(&broker_endpoint_server_cert),
    [SERIAL_CH_UPLINK] = CHANNEL_RECV(&broker_endpoint_uplink),
    [SERIAL_CH_DOWNLINK] = CHANNEL_SENDER(&broker_endpoint_downlink),
};

static struct pouch_gateway_node_info node;

static int open_channel(enum serial_channel ch)
{
    return serial_send_cmd(ch, SERIAL_CMD_OPEN);
}

static int close_channel(enum serial_channel ch)
{
    return serial_send_cmd(ch, SERIAL_CMD_CLOSE);
}

static int next(void)
{
    if (!node.server_cert_provisioned)
    {
        return open_channel(SERIAL_CH_SERVER_CERT);
    }
    if (!node.device_cert_provisioned)
    {
        return open_channel(SERIAL_CH_DEVICE_CERT);
    }

    // open link:
    int err = open_channel(SERIAL_CH_DOWNLINK);
    if (err)
    {
        return err;
    }

    err = open_channel(SERIAL_CH_UPLINK);
    if (err)
    {
        return err;
    }

    return 0;
}

static int recv_cmd(enum serial_channel ch, enum serial_cmd cmd)
{
    if (cmd == SERIAL_CMD_REQ_LINK)
    {
        return next();
    }

    // ignore
    return 0;
}

static int recv_data(enum serial_channel ch, const void *data, size_t len)
{
    struct channel *channel = &channels[ch];
    switch (channel->dir)
    {
        case SERIAL_CHANNEL_DIR_RECEIVER:
            return pouch_receiver_recv(channel->receiver, data, len);
        case SERIAL_CHANNEL_DIR_SENDER:
            return pouch_sender_recv(channel->sender, data, len);
    }
    return -EINVAL;
}

void serial_bearer_close(struct pouch_bearer *bearer, bool success)
{
    enum serial_channel ch = CHANNEL_OF(bearer) - &channels[0];
    close_channel(ch);
    switch (ch)
    {
        case SERIAL_CH_UPLINK:
        case SERIAL_CH_DOWNLINK:
            // end of transfer!
            break;
        default:
            next();
            break;
    }
}

int pouch_serial_broker_init(const struct device *dev)
{
    for (enum serial_channel ch = 0; ch < SERIAL_CHANNELS; ch++)
    {
        int err = serial_ch_init(ch, recv_data, recv_cmd);
        if (err)
        {
            return err;
        }

        channels[ch].bearer.close = serial_bearer_close;
        channels[ch].bearer.ready = serial_bearer_ready;
        channels[ch].bearer.send = serial_bearer_send;
        channels[ch].bearer.ctx = &node;
        channels[ch].bearer.maxlen = SERIAL_DATA_MAXLEN;
    }

    return open_channel(SERIAL_CH_INFO);
}
