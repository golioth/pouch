# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_POUCH)
    return()
endif()

if(NOT DEFINED POUCH_ACTIVE_TARGET)
    message(FATAL_ERROR "POUCH_ACTIVE_TARGET must be set before adding lib/zcbor_utils")
endif()

pouch_config_enabled(_pouch_zcbor_utils CONFIG_ZCBOR_UTILS)
if(NOT _pouch_zcbor_utils)
    return()
endif()

set(_pouch_zcbor_utils_root ${CMAKE_CURRENT_LIST_DIR})

target_sources(${POUCH_ACTIVE_TARGET} PRIVATE
    ${_pouch_zcbor_utils_root}/zcbor_utils.c
)

target_include_directories(${POUCH_ACTIVE_TARGET} PRIVATE
    ${_pouch_zcbor_utils_root}/include
)
