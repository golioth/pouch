# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_device_test)

set(POUCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

# Include paths: pouch public + private src headers
target_include_directories(app PRIVATE
    ${POUCH_DIR}/include
    ${POUCH_DIR}/port/include
    ${POUCH_DIR}/port/zephyr/include
    ${POUCH_DIR}/src
)

# The serial transport log macro needs CONFIG_POUCH_SERIAL_LOG_LEVEL, which is
# normally provided by Kconfig when CONFIG_POUCH is enabled. Since this test
# builds serial sources in isolation, define it directly.
target_compile_definitions(app PRIVATE CONFIG_POUCH_SERIAL_LOG_LEVEL=3)

# Serial transport sources (device side only, no broker)
target_sources(app PRIVATE
    ${POUCH_DIR}/src/transport/serial/device.c
    ${POUCH_DIR}/src/transport/serial/serial.c
    ${POUCH_DIR}/src/transport/serial/channel.c
    ${POUCH_DIR}/src/transport/serial/packet.c
)

# Test sources
target_sources(app PRIVATE
    src/stub_endpoints.c
    src/test_device.c
)
