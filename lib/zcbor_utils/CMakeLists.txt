# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZCBOR_UTILS)

cmake_minimum_required(VERSION 3.20.0)

include_directories(include)
target_sources(pouch PRIVATE zcbor_utils.c)
target_include_directories(pouch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pouch PUBLIC pouch_core)

endif(CONFIG_ZCBOR_UTILS)
