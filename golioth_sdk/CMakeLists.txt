# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_GOLIOTH)

cmake_minimum_required(VERSION 3.20.0)

target_sources(pouch PRIVATE dispatch.c)
target_link_options(pouch PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/dispatch.ld")
target_link_libraries(pouch PRIVATE zcbor_utils)

# Settings

if(CONFIG_GOLIOTH_SETTINGS)
    target_sources(pouch PRIVATE settings.c)
endif(CONFIG_GOLIOTH_SETTINGS)

if(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)
    target_sources(pouch PRIVATE settings_callbacks.c)
    target_link_options(pouch PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/settings_callbacks.ld")
endif(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)

# OTA

if(CONFIG_GOLIOTH_OTA)
    target_sources(pouch PRIVATE ota.c ota_upper.c)
    target_link_options(pouch PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/ota.ld")
endif(CONFIG_GOLIOTH_OTA)

endif(CONFIG_GOLIOTH)
