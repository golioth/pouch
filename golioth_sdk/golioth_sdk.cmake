# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_GOLIOTH)

    set(SDK_NAME "GOLIOTH_SDK")
    list(APPEND SDK_NAME_LIST ${SDK_NAME})

    list(APPEND ${SDK_NAME}_SRCS ${CMAKE_CURRENT_LIST_DIR}/dispatch.c)
    # Platform specific linker files for this SDK
    list(APPEND ${SDK_NAME}_LINKER_SECTION_ZEPHYR ${CMAKE_CURRENT_LIST_DIR}/dispatch.ld)
    list(APPEND ${SDK_NAME}_LINKER_SECTION_ESPIDF ${CMAKE_CURRENT_LIST_DIR}/dispatch.lf)

    # Settings

    if(CONFIG_GOLIOTH_SETTINGS)
        list(APPEND ${SDK_NAME}_SRCS ${CMAKE_CURRENT_LIST_DIR}/settings.c)
    endif(CONFIG_GOLIOTH_SETTINGS)

    if(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)
        list(APPEND ${SDK_NAME}_SRCS ${CMAKE_CURRENT_LIST_DIR}/settings_callbacks.c)
        # Platform specific linker files for this SDK
        list(APPEND ${SDK_NAME}_LINKER_SECTION_ZEPHYR ${CMAKE_CURRENT_LIST_DIR}/settings_callbacks.ld)
        list(APPEND ${SDK_NAME}_LINKER_SECTION_ESPIDF ${CMAKE_CURRENT_LIST_DIR}/settings_callbacks.lf)
    endif(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)

    # OTA

    if(CONFIG_GOLIOTH_OTA)
        list(APPEND ${SDK_NAME}_SRCS
            ${CMAKE_CURRENT_LIST_DIR}/ota.c
            ${CMAKE_CURRENT_LIST_DIR}/ota_upper.c
        )
        # Platform specific linker files for this SDK
        list(APPEND ${SDK_NAME}_LINKER_SECTION_ZEPHYR ${CMAKE_CURRENT_LIST_DIR}/ota.ld)
        list(APPEND ${SDK_NAME}_LINKER_SECTION_ESPIDF ${CMAKE_CURRENT_LIST_DIR}/ota.lf)
    endif(CONFIG_GOLIOTH_OTA)

endif(CONFIG_GOLIOTH)
