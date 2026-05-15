/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * MCUmgr Shell Client
 *
 * Uses a custom shell_mgmt_client to execute a shell command on the
 * remote server over the SMP UART transport, then prints the output.
 */

#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/smp/smp_client.h>

#include "shell_mgmt_client.h"

#define MAX_RETRIES 5
#define RETRY_DELAY_MS 1000

static struct smp_client_object smp_client;
static struct shell_mgmt_client shell_client;

int main(void)
{
    int rc;
    char output[512];

    printk("MCUmgr shell client starting\n");

    k_sleep(K_MSEC(500));

    rc = smp_client_object_init(&smp_client, SMP_SERIAL_TRANSPORT);
    if (rc != 0)
    {
        printk("SHELL FAILED: smp_client_object_init returned %d\n", rc);
        return rc;
    }

    shell_mgmt_client_init(&shell_client, &smp_client);

    printk("Client initialized, executing remote shell commands\n");

    const char *version_argv[] = {"kernel", "version", NULL};

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        rc = shell_mgmt_client_exec(&shell_client, version_argv, output, sizeof(output));
        if (rc == 0)
        {
            printk("SHELL OK: %s\n", output);
            printk("SHELL RET: %d\n", shell_client.ret_code);
            break;
        }

        printk("Shell attempt %d failed: %d, retrying...\n", attempt + 1, rc);
        k_sleep(K_MSEC(RETRY_DELAY_MS));
    }

    if (rc != 0)
    {
        printk("SHELL FAILED: %d after %d attempts\n", rc, MAX_RETRIES);
        return rc;
    }

    const char *uptime_argv[] = {"kernel", "uptime", NULL};

    rc = shell_mgmt_client_exec(&shell_client, uptime_argv, output, sizeof(output));
    if (rc == 0)
    {
        printk("REMOTE UPTIME: %s\n", output);
    }
    else
    {
        printk("REMOTE UPTIME FAILED: %d\n", rc);
        return rc;
    }

    printk("LOCAL UPTIME: %lld ms\n", k_uptime_get());

    return 0;
}
