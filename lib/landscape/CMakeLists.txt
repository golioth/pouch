# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(include)

zephyr_library()

zephyr_library_sources(
    bpickle.c
    register.c
)
