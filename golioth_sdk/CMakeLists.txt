# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

pouch_config_enabled(_pouch_golioth CONFIG_GOLIOTH)
if(NOT _pouch_golioth)
    return()
endif()

if(NOT DEFINED POUCH_ACTIVE_TARGET)
    message(FATAL_ERROR "POUCH_ACTIVE_TARGET must be set before adding golioth_sdk")
endif()

set(_pouch_golioth_sdk_root ${CMAKE_CURRENT_LIST_DIR})
set(_pouch_target ${POUCH_ACTIVE_TARGET})

target_sources(${_pouch_target} PRIVATE
    ${_pouch_golioth_sdk_root}/dispatch.c
)

target_include_directories(${_pouch_target} PRIVATE
    ${_pouch_golioth_sdk_root}
)

pouch_config_enabled(_pouch_golioth_settings CONFIG_GOLIOTH_SETTINGS)
if(_pouch_golioth_settings)
    target_sources(${_pouch_target} PRIVATE ${_pouch_golioth_sdk_root}/settings.c)
endif()

pouch_config_enabled(_pouch_golioth_callbacks CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)
if(_pouch_golioth_callbacks)
    target_sources(${_pouch_target} PRIVATE ${_pouch_golioth_sdk_root}/settings_callbacks.c)
endif()

pouch_config_enabled(_pouch_golioth_ota CONFIG_GOLIOTH_OTA)
if(_pouch_golioth_ota)
    target_sources(${_pouch_target} PRIVATE
        ${_pouch_golioth_sdk_root}/hex.c
        ${_pouch_golioth_sdk_root}/ota_upper.c
        ${_pouch_golioth_sdk_root}/ota.c
    )
endif()

set(_pouch_golioth_ldfiles)
list(APPEND _pouch_golioth_ldfiles ${_pouch_golioth_sdk_root}/dispatch.lf)

pouch_config_enabled(_pouch_golioth_settings CONFIG_GOLIOTH_SETTINGS)
if(_pouch_golioth_settings)
    list(APPEND _pouch_golioth_ldfiles ${_pouch_golioth_sdk_root}/settings_callbacks.lf)
endif()

pouch_config_enabled(_pouch_golioth_ota CONFIG_GOLIOTH_OTA)
if(_pouch_golioth_ota)
    list(APPEND _pouch_golioth_ldfiles ${_pouch_golioth_sdk_root}/ota.lf)
endif()

set(POUCH_GOLIOTH_SDK_LINKER_FILES ${_pouch_golioth_ldfiles} PARENT_SCOPE)
