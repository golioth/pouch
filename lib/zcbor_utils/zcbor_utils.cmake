# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZCBOR_UTILS)

    list(APPEND POUCH_LIB_INC_DIRS
        ${CMAKE_CURRENT_LIST_DIR}/include
    )

    list(APPEND POUCH_LIB_SRCS
        ${CMAKE_CURRENT_LIST_DIR}/zcbor_utils.c
    )

endif(CONFIG_ZCBOR_UTILS)
