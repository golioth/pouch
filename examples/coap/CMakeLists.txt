# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(coap_example)

target_sources(app PRIVATE
  src/main.c
  src/credentials.c
  src/fatal_error.c
  src/fw_update.c
)

generate_inc_file_for_target(app src/isrgrootx1.der ${ZEPHYR_BINARY_DIR}/include/generated/pouch-coap-ca-cert.inc)
