# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_GOLIOTH)

cmake_minimum_required(VERSION 3.5)

project(golioth_sdk)
add_library(golioth_sdk STATIC dispatch.c)
target_link_options(golioth_sdk PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/dispatch.ld")
target_link_libraries(golioth_sdk PRIVATE zcbor_utils)
target_link_libraries(golioth_sdk PRIVATE pouch)

# Settings

if(CONFIG_GOLIOTH_SETTINGS)
    target_sources(golioth_sdk PRIVATE settings.c)
endif(CONFIG_GOLIOTH_SETTINGS)

if(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)
    target_sources(golioth_sdk PRIVATE settings_callbacks.c)
    target_link_options(golioth_sdk PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/settings_callbacks.ld")
endif(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS)

# OTA

if(CONFIG_GOLIOTH_OTA)
    target_sources(golioth_sdk PRIVATE ota.c ota_upper.c)
    target_link_options(golioth_sdk PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/ota.ld")
endif(CONFIG_GOLIOTH_OTA)

endif(CONFIG_GOLIOTH)
