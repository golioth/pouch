# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

include(${CMAKE_CURRENT_LIST_DIR}/cmake/utils.cmake)
set(_golioth_sdk_root ${CMAKE_CURRENT_LIST_DIR})

golioth_sdk_config_enabled(_golioth_sdk CONFIG_GOLIOTH)
golioth_sdk_config_enabled(_golioth_settings CONFIG_GOLIOTH_SETTINGS)
golioth_sdk_config_enabled(_golioth_ota CONFIG_GOLIOTH_OTA)

set(_golioth_sdk_srcs "")
set(_golioth_sdk_linker_files_no_extension "")

if(_golioth_sdk)
    list(APPEND _golioth_sdk_srcs ${_golioth_sdk_root}/dispatch.c)
    list(APPEND _golioth_sdk_linker_files_no_extension ${_golioth_sdk_root}/dispatch)

    if(_golioth_settings)
        list(APPEND _golioth_sdk_srcs
            ${_golioth_sdk_root}/settings.c
            ${_golioth_sdk_root}/settings_callbacks.c
        )
        list(APPEND _golioth_sdk_linker_files_no_extension ${_golioth_sdk_root}/settings_callbacks)
    endif()

    if(_golioth_ota)
        list(APPEND _golioth_sdk_srcs
            ${_golioth_sdk_root}/hex.c
            ${_golioth_sdk_root}/ota_upper.c
            ${_golioth_sdk_root}/ota.c
        )
        list(APPEND _golioth_sdk_linker_files_no_extension ${_golioth_sdk_root}/ota)
    endif()

    if(_golioth_settings OR _golioth_ota)
        list(APPEND _golioth_sdk_srcs ${_golioth_sdk_root}/zcbor_utils.c)
    endif()
endif()

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_idf_component.cmake)
elseif(Zephyr_DIR)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/zephyr_module.cmake)
endif()
