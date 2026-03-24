/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <pouch/transport/types.h>

/**
 * Pouch transport endpoint definition.
 */
struct pouch_endpoint
{
    /**
     * Start callback, signalling the start of a new transfer.
     *
     * @return 0 if the transfer could be started, or an error code if the endpoint is not ready.
     */
    int (*start)(void);
    /**
     * End callback, signalling the end of a transfer.
     */
    void (*end)(bool success);
    /**
     * Receive callback, pushing data to the endpoint.
     * This callback is only used in receiver endpoints.
     *
     * @return 0 if the data was processed successfully, or an error code if the data could not be
     * processed.
     */
    int (*recv)(const void *buf, size_t len);
    /**
     * Send callback, pulling data from the endpoint.
     * This callback is only used in sender endpoints.
     *
     * @param[out] dst Buffer the endpoint should fill with data.
     * @param[in,out] dst_len Set to the size of the dst buffer. Should be changed by the endpoint
     * to match the size of the dst data.
     *
     * @retval POUCH_NO_MORE_DATA There's no more data to send in this transfer, and the transfer
     * should end once this data has been sent.
     * @retval POUCH_MORE_DATA More data is available, and the transfer should keep calling the
     * endpoint.
     * @retval POUCH_ERROR An error occured, and the transfer should be aborted.
     */
    enum pouch_result (*send)(void *dst, size_t *dst_len);
};

extern const struct pouch_endpoint pouch_endpoint_info;
extern const struct pouch_endpoint pouch_endpoint_device_cert;
extern const struct pouch_endpoint pouch_endpoint_server_cert;
extern const struct pouch_endpoint pouch_endpoint_uplink;
extern const struct pouch_endpoint pouch_endpoint_downlink;
