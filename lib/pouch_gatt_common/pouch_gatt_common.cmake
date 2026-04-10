# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_POUCH_TRANSPORT_GATT_COMMON)

    list(APPEND POUCH_LIB_INC_DIRS
        ${CMAKE_CURRENT_LIST_DIR}/include
    )

    list(APPEND POUCH_LIB_SRCS
        ${CMAKE_CURRENT_LIST_DIR}/packetizer.c
        ${CMAKE_CURRENT_LIST_DIR}/receiver.c
        ${CMAKE_CURRENT_LIST_DIR}/sender.c
    )

endif(CONFIG_POUCH_TRANSPORT_GATT_COMMON)
