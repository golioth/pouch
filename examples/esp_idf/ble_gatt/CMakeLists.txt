# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)

list(APPEND EXTRA_COMPONENT_DIRS
        ${CMAKE_SOURCE_DIR}/../../../port/esp_idf/components
    )

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(pouch_ble_gatt_example)
