# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_link_libraries(pouch)

zephyr_library_sources(
    pouch_http.c
)

zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
