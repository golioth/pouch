# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

include(${ZEPHYR_POUCH_MODULE_DIR}/cmake/native_parallel.cmake)

ExternalZephyrProject_Add(
    APPLICATION client
    SOURCE_DIR  ${APP_DIR}/client
    BOARD       ${BOARD}
)

# Connect server uart_1 <-> client uart_1 over a virtual serial link.
native_parallel_serial_link(
    ${DEFAULT_IMAGE} uart_1
    client           uart_1
)
