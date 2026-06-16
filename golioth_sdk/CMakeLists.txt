# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(NOT (CONFIG_GOLIOTH OR "${CONFIG_GOLIOTH}" STREQUAL "y"))
    return()
endif()

set(_golioth_sdk_root ${CMAKE_CURRENT_LIST_DIR})

# Note: linker files for golioth_sdk are added in cmake/golioth_sdk_linker_files.cmake. Each port
# should call golioth_sdk_get_linker_files_no_ext() to access this list, then iterate and append the
# appropriate extension for each entry. This ensures that ports can access linker files when needed,
# most notably ESP-IDF which needs access to all linker files during early expansion.

if(Zephyr_DIR)
    # Workaround Zephyr not linking target_include_directories at the app level
    zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)
else()
    target_include_directories(${POUCH_ACTIVE_TARGET} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
endif()

target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/dispatch.c)

if(CONFIG_GOLIOTH_SETTINGS OR "${CONFIG_GOLIOTH_SETTINGS}" STREQUAL "y")
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/settings.c)
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/settings_callbacks.c)
    set(NEEDS_ZCBOR_UTILS TRUE)
endif()

if(CONFIG_GOLIOTH_OTA OR "${CONFIG_GOLIOTH_OTA}" STREQUAL "y")
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/hex.c)
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/ota_upper.c)
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/ota.c)
    set(NEEDS_ZCBOR_UTILS TRUE)
endif()

if(NEEDS_ZCBOR_UTILS)
    target_sources(${POUCH_ACTIVE_TARGET} PRIVATE ${_golioth_sdk_root}/zcbor_utils.c)
endif()
