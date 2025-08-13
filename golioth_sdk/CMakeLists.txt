# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(NOT CONFIG_GOLIOTH)
    return()
endif()

zephyr_library()

zephyr_library_sources(
    dispatch.c
)

zephyr_linker_sources(SECTIONS dispatch.ld)

# Settings

zephyr_library_sources_ifdef(CONFIG_GOLIOTH_SETTINGS settings.c)
zephyr_library_sources_ifdef(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS settings_callbacks.c)
zephyr_linker_sources_ifdef(CONFIG_GOLIOTH_SETTINGS_FRONTEND_CALLBACKS
                            SECTIONS
                            settings_callbacks.ld)

# OTA

zephyr_library_sources_ifdef(CONFIG_GOLIOTH_OTA ota.c ota_upper.c)
zephyr_linker_sources_ifdef(CONFIG_GOLIOTH_OTA SECTIONS ota.ld)
