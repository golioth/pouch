/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * MCUmgr Echo Server
 *
 * The MCUmgr subsystem auto-registers at SYS_INIT time:
 * - SMP UART transport (via CONFIG_MCUMGR_TRANSPORT_UART)
 * - OS management group with echo handler (via CONFIG_MCUMGR_GRP_OS)
 *
 * All this main() needs to do is keep the system alive.
 */

#include <zephyr/kernel.h>

int main(void)
{
    printk("MCUmgr echo server ready\n");
    k_sleep(K_FOREVER);
    return 0;
}
