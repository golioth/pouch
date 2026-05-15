/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SHELL_MGMT_CLIENT_H_
#define SHELL_MGMT_CLIENT_H_

#include <zephyr/mgmt/mcumgr/smp/smp_client.h>

/**
 * @brief Shell management client context.
 *
 * Stores state for one in-flight shell exec command.  Only one command
 * may be active at a time (serialized by an internal mutex).
 */
struct shell_mgmt_client
{
    struct smp_client_object *smp_client;
    int status;
    char *output_buf;
    size_t output_buf_size;
    size_t output_len;
    int ret_code;
};

/**
 * @brief Initialize a shell management client.
 *
 * @param client  Client context to initialize.
 * @param smp_client  SMP client object (must already be initialized).
 */
void shell_mgmt_client_init(struct shell_mgmt_client *client, struct smp_client_object *smp_client);

/**
 * @brief Execute a shell command on the remote device.
 *
 * Sends an SMP shell exec request with the given argument vector,
 * blocks until a response arrives (or timeout), and copies the
 * command output into @p output_buf.
 *
 * @param client         Client context.
 * @param argv           NULL-terminated array of command tokens
 *                       (e.g. {"kernel", "version", NULL}).
 * @param output_buf     Buffer to receive shell output text.
 * @param output_buf_size  Size of @p output_buf.
 *
 * @return 0 on success, negative mcumgr error code on failure.
 */
int shell_mgmt_client_exec(struct shell_mgmt_client *client,
                           const char **argv,
                           char *output_buf,
                           size_t output_buf_size);

#endif /* SHELL_MGMT_CLIENT_H_ */
