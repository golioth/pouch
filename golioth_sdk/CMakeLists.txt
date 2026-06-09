# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_idf_component.cmake)
    return()
endif()

pouch_config_enabled(_golioth_sdk CONFIG_GOLIOTH)
if(NOT _golioth_sdk)
    return()
endif()

if(NOT DEFINED POUCH_ACTIVE_TARGET)
    return()
endif()

set(_golioth_sdk_root ${CMAKE_CURRENT_LIST_DIR})
set(_golioth_target ${POUCH_ACTIVE_TARGET})

target_sources(${_golioth_target} PRIVATE
    ${_golioth_sdk_root}/dispatch.c
)

target_include_directories(${_golioth_target} PRIVATE
    ${_golioth_sdk_root}
)

pouch_config_enabled(_golioth_settings CONFIG_GOLIOTH_SETTINGS)
if(_golioth_settings)
    target_sources(${_golioth_target} PRIVATE ${_golioth_sdk_root}/settings.c)
endif()

pouch_config_enabled(_golioth_callbacks CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)
if(_golioth_callbacks)
    target_sources(${_golioth_target} PRIVATE ${_golioth_sdk_root}/settings_callbacks.c)
endif()

pouch_config_enabled(_golioth_ota CONFIG_GOLIOTH_OTA)
if(_golioth_ota)
    target_sources(${_golioth_target} PRIVATE
        ${_golioth_sdk_root}/hex.c
        ${_golioth_sdk_root}/ota_upper.c
        ${_golioth_sdk_root}/ota.c
    )
endif()

pouch_config_enabled(_golioth_zcbor_utils CONFIG_ZCBOR_UTILS)
if(_golioth_zcbor_utils)
    target_sources(${_golioth_target} PRIVATE
        ${_golioth_sdk_root}/zcbor_utils.c
    )
    target_include_directories(${_golioth_target} PRIVATE
        ${_golioth_sdk_root}/include
    )
endif()
