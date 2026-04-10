# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_GOLIOTH)

    set(SDK_NAME "GOLIOTH_SDK")
    list(APPEND SDK_NAME_LIST ${SDK_NAME})

    list(APPEND ${SDK_NAME}_SRCS ${CMAKE_CURRENT_LIST_DIR}/dispatch.c)
    list(APPEND ${SDK_NAME}_LINKER_SECTION ${CMAKE_CURRENT_LIST_DIR}/dispatch)

    # Settings

    if(CONFIG_GOLIOTH_SETTINGS)
        list(APPEND ${SDK_NAME}_SRCS ${CMAKE_CURRENT_LIST_DIR}/settings.c)
    endif(CONFIG_GOLIOTH_SETTINGS)

    if(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)
        list(APPEND ${SDK_NAME}_SRCS ${CMAKE_CURRENT_LIST_DIR}/settings_callbacks.c)
        list(APPEND ${SDK_NAME}_LINKER_SECTION ${CMAKE_CURRENT_LIST_DIR}/settings_callbacks)
    endif(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)

    # OTA

    if(CONFIG_GOLIOTH_OTA)
        list(APPEND ${SDK_NAME}_SRCS
            ${CMAKE_CURRENT_LIST_DIR}/ota.c
            ${CMAKE_CURRENT_LIST_DIR}/ota_upper.c
        )
        list(APPEND ${SDK_NAME}_LINKER_SECTION ${CMAKE_CURRENT_LIST_DIR}/ota)
    endif(CONFIG_GOLIOTH_OTA)

endif(CONFIG_GOLIOTH)
