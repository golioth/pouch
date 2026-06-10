# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

include(${CMAKE_CURRENT_LIST_DIR}/cmake/utils.cmake)
set(_golioth_sdk_root ${CMAKE_CURRENT_LIST_DIR})

golioth_sdk_config_enabled(_golioth_sdk CONFIG_GOLIOTH)
golioth_sdk_config_enabled(_golioth_settings CONFIG_GOLIOTH_SETTINGS)
golioth_sdk_config_enabled(_golioth_ota CONFIG_GOLIOTH_OTA)

reset_registered_linker_files()

set(_golioth_sdk_srcs "")

if(_golioth_sdk)
    list(APPEND _golioth_sdk_srcs ${_golioth_sdk_root}/dispatch.c)
    register_linker_file(${_golioth_sdk_root}/dispatch)

    if(_golioth_settings)
        list(APPEND _golioth_sdk_srcs
            ${_golioth_sdk_root}/settings.c
            ${_golioth_sdk_root}/settings_callbacks.c
        )
        register_linker_file(${_golioth_sdk_root}/settings_callbacks)
    endif()

    if(_golioth_ota)
        list(APPEND _golioth_sdk_srcs
            ${_golioth_sdk_root}/hex.c
            ${_golioth_sdk_root}/ota_upper.c
            ${_golioth_sdk_root}/ota.c
        )
        register_linker_file(${_golioth_sdk_root}/ota)
    endif()

    if(_golioth_settings OR _golioth_ota)
        list(APPEND _golioth_sdk_srcs ${_golioth_sdk_root}/zcbor_utils.c)
    endif()
endif()

get_registered_linker_files(_golioth_sdk_linker_files)

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_idf_component.cmake)
elseif(COMMAND zephyr_library_named)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/zephyr_module.cmake)
endif()
