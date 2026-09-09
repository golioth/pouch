# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_fw_test)

set(POUCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

target_include_directories(app PRIVATE
    ${POUCH_DIR}/include
    ${POUCH_DIR}/port/include
    ${POUCH_DIR}/port/zephyr/include
    ${POUCH_DIR}/src
)

# The relay's log macro needs CONFIG_POUCH_COMMON_LOG_LEVEL, normally provided
# by Kconfig; define it directly since this builds the relay in isolation.
target_compile_definitions(app PRIVATE CONFIG_POUCH_COMMON_LOG_LEVEL=3)

# The firmware relay under test. The port primitives it uses come from the
# pouch module's own build, as they do for the other serial test suites.
target_sources(app PRIVATE
    ${POUCH_DIR}/src/transport/endpoints/device/fw.c
    ${POUCH_DIR}/src/transport/endpoints/device/fw_status.c
)

target_sources(app PRIVATE src/test_fw.c)
