# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(http_transport)

target_sources(app PRIVATE
  src/main.c
  src/credentials.c
  src/conn_monitor.c
)

zephyr_sources_ifdef(CONFIG_GOLIOTH_OTA src/fw_update.c)

if (NOT CONFIG_POUCH_HTTP_TLS_LOAD_CA_FROM_FILESYSTEM)
    generate_inc_file_for_target(app src/isrgrootx1.der
        ${ZEPHYR_BINARY_DIR}/include/generated/pouch-ca-crt.inc)
endif (NOT CONFIG_POUCH_HTTP_TLS_LOAD_CA_FROM_FILESYSTEM)
