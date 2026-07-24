# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rpmsg_exchange_test)

set(POUCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

# Golioth SDK path (needed by gateway/types.h included from broker.c)
set(GOLIOTH_SDK ${ZEPHYR_BASE}/../modules/lib/golioth-firmware-sdk)

target_include_directories(app PRIVATE
    ${POUCH_DIR}/include
    ${POUCH_DIR}/port/include
    ${POUCH_DIR}/port/zephyr/include
    ${POUCH_DIR}/port/zephyr/transport/serial   # uart_framing.h
    ${POUCH_DIR}/src
    ${GOLIOTH_SDK}/include
    ${GOLIOTH_SDK}/port/zephyr/include
    ${POUCH_DIR}/tests/pouch/serial/exchange/src # stub_endpoints.h
)

# The serial transport log macro needs CONFIG_POUCH_SERIAL_LOG_LEVEL, normally
# provided by Kconfig; define it directly since we build serial sources in
# isolation.
target_compile_definitions(app PRIVATE CONFIG_POUCH_SERIAL_LOG_LEVEL=3)

# Serial transport core + the UART framing under test, plus the shared stub
# endpoints from the serial exchange test.
target_sources(app PRIVATE
    ${POUCH_DIR}/src/transport/serial/broker.c
    ${POUCH_DIR}/src/transport/serial/device.c
    ${POUCH_DIR}/src/transport/serial/serial.c
    ${POUCH_DIR}/src/transport/serial/channel.c
    ${POUCH_DIR}/src/transport/serial/packet.c
    ${POUCH_DIR}/port/zephyr/transport/serial/uart_framing.c
    ${POUCH_DIR}/tests/pouch/serial/exchange/src/stub_endpoints.c
)

target_sources(app PRIVATE src/test_rpmsg_exchange.c)
