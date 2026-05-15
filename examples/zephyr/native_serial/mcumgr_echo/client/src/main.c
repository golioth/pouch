/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * MCUmgr Echo Client
 *
 * Uses the SMP client framework to send an OS echo command to the
 * server over UART, then prints the result.
 */

#include <zephyr/kernel.h>
#include <zephyr/mgmt/mcumgr/smp/smp_client.h>
#include <zephyr/mgmt/mcumgr/grp/os_mgmt/os_mgmt_client.h>

#define ECHO_STRING "test123"
#define MAX_RETRIES 5
#define RETRY_DELAY_MS 1000

static struct smp_client_object smp_client;
static struct os_mgmt_client os_client;

int main(void)
{
    int rc;

    printk("MCUmgr echo client starting\n");

    k_sleep(K_MSEC(500));

    rc = smp_client_object_init(&smp_client, SMP_SERIAL_TRANSPORT);
    if (rc != 0)
    {
        printk("ECHO FAILED: smp_client_object_init returned %d\n", rc);
        return rc;
    }

    os_mgmt_client_init(&os_client, &smp_client);

    printk("Client initialized, sending echo\n");

    /* Attempt echo with retries (server might not be fully ready) */
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
    {
        rc = os_mgmt_client_echo(&os_client, ECHO_STRING, sizeof(ECHO_STRING));
        if (rc == 0)
        {
            printk("ECHO OK: %s\n", ECHO_STRING);
            return 0;
        }

        printk("Echo attempt %d failed: %d, retrying...\n", attempt + 1, rc);
        k_sleep(K_MSEC(RETRY_DELAY_MS));
    }

    printk("ECHO FAILED: %d after %d attempts\n", rc, MAX_RETRIES);
    return rc;
}
