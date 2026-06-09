# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

set(_golioth_sdk_root ${CMAKE_CURRENT_LIST_DIR}/..)

set(_golioth_sdk_srcs
    ${_golioth_sdk_root}/dispatch.c
)

set(_golioth_sdk_linker_files
    ${_golioth_sdk_root}/dispatch.lf
)

if("${CONFIG_GOLIOTH_SETTINGS}" STREQUAL "y")
    list(APPEND _golioth_sdk_srcs
        ${_golioth_sdk_root}/settings.c
        ${_golioth_sdk_root}/settings_callbacks.c
    )
    list(APPEND _golioth_sdk_linker_files
        ${_golioth_sdk_root}/settings_callbacks.lf
    )
endif()

if("${CONFIG_GOLIOTH_OTA}" STREQUAL "y")
    list(APPEND _golioth_sdk_srcs
        ${_golioth_sdk_root}/hex.c
        ${_golioth_sdk_root}/ota.c
        ${_golioth_sdk_root}/ota_upper.c
    )
    list(APPEND _golioth_sdk_linker_files
        ${_golioth_sdk_root}/ota.lf
    )
endif()

if("${CONFIG_GOLIOTH_SETTINGS}" STREQUAL "y" OR "${CONFIG_GOLIOTH_OTA}" STREQUAL "y")
    list(APPEND _golioth_sdk_srcs ${_golioth_sdk_root}/zcbor_utils.c)
endif()

idf_component_register(SRCS
                        ${_golioth_sdk_srcs}

                       INCLUDE_DIRS
                        ${_golioth_sdk_root}
                        ${_golioth_sdk_root}/include

                       LDFRAGMENTS
                        ${_golioth_sdk_linker_files}

                       REQUIRES
                        pouch
                       )

idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_link_libraries(${COMPONENT_LIB} PUBLIC zcbor)
endif()
