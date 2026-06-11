# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# Zephyr BT GATT broker (gateway side) library.  Implements the
# pouch_bearer interface in terms of GATT central writes and notifies
# to peripherals running the Pouch GATT service.

zephyr_library()

zephyr_library_link_libraries(pouch)

zephyr_library_sources(
    bond.c
    discover.c
    broker.c
    scan.c
)

zephyr_library_include_directories(${ZEPHYR_POUCH_MODULE_DIR}/src)
