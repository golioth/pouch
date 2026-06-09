# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_broker_test)

set(POUCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

# Golioth SDK path (needed by gateway/types.h included from broker.c)
set(GOLIOTH_SDK ${ZEPHYR_BASE}/../modules/lib/golioth-firmware-sdk)

# Include paths: pouch public + private src headers
target_include_directories(app PRIVATE
    ${POUCH_DIR}/include
    ${POUCH_DIR}/port/include
    ${POUCH_DIR}/port/zephyr/include
    ${POUCH_DIR}/src
    ${GOLIOTH_SDK}/include
    ${GOLIOTH_SDK}/port/zephyr/include
)

# The serial transport log macro needs CONFIG_POUCH_SERIAL_LOG_LEVEL, which is
# normally provided by Kconfig when CONFIG_POUCH is enabled. Since this test
# builds serial sources in isolation, define it directly.
target_compile_definitions(app PRIVATE CONFIG_POUCH_SERIAL_LOG_LEVEL=3)

# Serial transport sources
target_sources(app PRIVATE
    ${POUCH_DIR}/src/transport/serial/broker.c
    ${POUCH_DIR}/src/transport/serial/serial.c
    ${POUCH_DIR}/src/transport/serial/channel.c
    ${POUCH_DIR}/src/transport/serial/packet.c
)

# Test sources
target_sources(app PRIVATE
    src/stub_endpoints.c
    src/test_broker.c
)
