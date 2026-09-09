# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_fw_url_test)

set(POUCH_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../..)

target_include_directories(app PRIVATE
    ${POUCH_DIR}/include
    ${POUCH_DIR}/port/include
    ${POUCH_DIR}/port/zephyr/include
    ${POUCH_DIR}/src
)

# The endpoint log macros need CONFIG_POUCH_COMMON_LOG_LEVEL, normally provided
# by Kconfig; define it directly since this builds the endpoints in isolation.
target_compile_definitions(app PRIVATE CONFIG_POUCH_COMMON_LOG_LEVEL=3)

# Keep the record small enough that a test can hold a few of them on the stack;
# the default is 1024, which only matters for what a real signer produces.
target_compile_definitions(app PRIVATE CONFIG_POUCH_SERIAL_FW_URL_MAX_LEN=256)

# The handoff under test, and the verdict channel it shares with the relay. The
# port primitives come from the pouch module's own build, as they do for the
# other serial test suites.
target_sources(app PRIVATE
    ${POUCH_DIR}/src/transport/endpoints/device/fw_url.c
    ${POUCH_DIR}/src/transport/endpoints/device/fw_status.c
)

target_sources(app PRIVATE src/test_fw_url.c)
