# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

set(requires "")

set(POUCH_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(POUCH_PORT "${POUCH_ROOT}/port")

idf_component_register(SRCS
                        "${POUCH_PORT}/freertos/freertos_port_layer.c"
                         ${POUCH_COMMON_SRCS}
                         ${POUCH_LIB_SRCS}

                       INCLUDE_DIRS
                         "${POUCH_COMMON_INC_DIRS}"
                         "${POUCH_LIB_INC_DIRS}"
                         "${POUCH_PORT}/esp_idf"  # esp_idf/pouch_port_macros_internal.h
                         "${POUCH_PORT}/include"  # pouch/port.h
                         "${POUCH_PORT}/freertos" # port/freertos

                       REQUIRES
                         mbedtls
                         zcbor
                       )
