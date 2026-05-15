/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file shell_mgmt_client.c
 *
 * Minimal MCUmgr shell management client.  Sends a shell exec command
 * (SMP group 9, command 0) to a remote MCUmgr server and returns the
 * captured output.
 *
 * Protocol:
 *   Request  (MGMT_OP_WRITE): { "argv": ["cmd", "arg1", ...] }
 *   Response:                  { "o": "<output>", "ret": <int> }
 *
 * Implementation follows the same pattern as the upstream
 * os_mgmt_client (global mutex + active_client + binary semaphore).
 */

#include <string.h>
#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>

#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp_client.h>
#include <zephyr/mgmt/mcumgr/grp/shell_mgmt/shell_mgmt.h>

#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <mgmt/mcumgr/transport/smp_internal.h>

#include "shell_mgmt_client.h"

/*
 * Number of zcbor states the client allocates on its own stack.
 * The shell client only services SMP *replies*, not inbound requests,
 * so the server-side CONFIG_MCUMGR_SMP_CBOR_MAX_DECODING_LEVELS does
 * not apply.  4 = 2 nesting levels (map + list) needed by the encode
 * path plus 2 backup states that zcbor_map_decode_bulk() consumes.
 */
#define SHELL_CLIENT_CBOR_STATES 4

/* Only one shell command may be in flight at a time. */
static struct shell_mgmt_client *active_client;
static K_SEM_DEFINE(shell_client_sem, 0, 1);
static K_MUTEX_DEFINE(shell_client_mutex);

static int shell_exec_res_fn(struct net_buf *nb, void *user_data)
{
    struct zcbor_string output = {0};
    int64_t ret_code = 0;
    zcbor_state_t zsd[SHELL_CLIENT_CBOR_STATES + 2];
    size_t decoded;
    int rc;

    if (!nb)
    {
        active_client->status = MGMT_ERR_ETIMEOUT;
        goto end;
    }

    zcbor_new_decode_state(zsd, ARRAY_SIZE(zsd), nb->data, nb->len, 1, NULL, 0);

    struct zcbor_map_decode_key_val response_map[] = {
        ZCBOR_MAP_DECODE_KEY_DECODER("o", zcbor_tstr_decode, &output),
        ZCBOR_MAP_DECODE_KEY_DECODER("ret", zcbor_int64_decode, &ret_code),
    };

    rc = zcbor_map_decode_bulk(zsd, response_map, ARRAY_SIZE(response_map), &decoded);
    if (rc != 0)
    {
        active_client->status = MGMT_ERR_ECORRUPT;
        goto end;
    }

    size_t copy_len = MIN(output.len, active_client->output_buf_size - 1);

    memcpy(active_client->output_buf, output.value, copy_len);
    active_client->output_buf[copy_len] = '\0';
    active_client->output_len = copy_len;
    active_client->ret_code = (int) ret_code;
    active_client->status = MGMT_ERR_EOK;

end:
    rc = active_client->status;
    k_sem_give(user_data);
    return rc;
}

void shell_mgmt_client_init(struct shell_mgmt_client *client, struct smp_client_object *smp_client)
{
    client->smp_client = smp_client;
}

int shell_mgmt_client_exec(struct shell_mgmt_client *client,
                           const char **argv,
                           char *output_buf,
                           size_t output_buf_size)
{
    struct net_buf *nb;
    int rc;
    bool ok;
    zcbor_state_t zse[SHELL_CLIENT_CBOR_STATES];

    k_mutex_lock(&shell_client_mutex, K_FOREVER);
    active_client = client;
    client->output_buf = output_buf;
    client->output_buf_size = output_buf_size;
    client->output_len = 0;
    client->ret_code = -1;

    /* Allocate buffer with SMP header for SHELL_MGMT EXEC (write op). */
    nb = smp_client_buf_allocation(client->smp_client,
                                   MGMT_GROUP_ID_SHELL,
                                   SHELL_MGMT_ID_EXEC,
                                   MGMT_OP_WRITE,
                                   SMP_MCUMGR_VERSION_1);
    if (!nb)
    {
        rc = client->status = MGMT_ERR_ENOMEM;
        goto end;
    }

    /* Encode CBOR request: { "argv": ["tok0", "tok1", ...] } */
    zcbor_new_encode_state(zse, ARRAY_SIZE(zse), nb->data + nb->len, net_buf_tailroom(nb), 0);

    ok = zcbor_map_start_encode(zse, 1) && zcbor_tstr_put_lit(zse, "argv")
        && zcbor_list_start_encode(zse, 10);

    for (int i = 0; argv[i] != NULL && ok; i++)
    {
        ok = zcbor_tstr_put_term(zse, argv[i], 256);
    }

    ok = ok && zcbor_list_end_encode(zse, 10) && zcbor_map_end_encode(zse, 1);

    if (!ok)
    {
        smp_packet_free(nb);
        rc = client->status = MGMT_ERR_ENOMEM;
        goto end;
    }

    nb->len = zse->payload - nb->data;

    k_sem_reset(&shell_client_sem);
    rc = smp_client_send_cmd(client->smp_client,
                             nb,
                             shell_exec_res_fn,
                             &shell_client_sem,
                             CONFIG_SMP_CMD_DEFAULT_LIFE_TIME);
    if (rc)
    {
        smp_packet_free(nb);
    }
    else
    {
        k_sem_take(&shell_client_sem, K_FOREVER);
        rc = client->status;
    }

end:
    active_client = NULL;
    k_mutex_unlock(&shell_client_mutex);
    return rc;
}
