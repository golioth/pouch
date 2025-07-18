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
