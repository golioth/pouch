# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_gatt_example)

target_sources(app PRIVATE
  src/main.c
  src/credentials.c
  src/fatal_error.c
  src/ble_peripheral.c
)

if(CONFIG_EXAMPLE_FW_UPDATE_SHA256_VERIFY)
    target_sources(app PRIVATE src/fw_update_test.c)
elseif(CONFIG_EXAMPLE_FW_UPDATE_FLASH)
    target_sources(app PRIVATE src/fw_update.c)
endif()
