# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(serial_gateway_example)

# Apart from the SPI broker, this is the same gateway as the Bluetooth gateway example, so share its
# credential and network handling.
set(GATEWAY_EXAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../gateway)

target_include_directories(app PRIVATE ${GATEWAY_EXAMPLE_DIR}/src)

target_sources(app PRIVATE
  src/main.c
  ${GATEWAY_EXAMPLE_DIR}/src/credentials.c
  ${GATEWAY_EXAMPLE_DIR}/src/fatal_error.c
)

if(CONFIG_WIFI)
  target_sources(app PRIVATE ${GATEWAY_EXAMPLE_DIR}/src/wifi.c)
endif()

if (NOT CONFIG_EXAMPLE_COAP_CLIENT_DTLS_LOAD_CA_FROM_FILESYSTEM)
  generate_inc_file_for_target(app
    ${CMAKE_CURRENT_SOURCE_DIR}/../http_client/src/isrgrootx1.der
    ${ZEPHYR_BINARY_DIR}/include/generated/pouch-ca-crt.inc)
endif()
