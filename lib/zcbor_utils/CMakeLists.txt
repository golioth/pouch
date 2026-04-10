# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ZCBOR_UTILS)

cmake_minimum_required(VERSION 3.5)

project(zcbor_utils C)
include_directories(include)
add_library(zcbor_utils STATIC zcbor_utils.c)
target_include_directories(zcbor_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(zcbor_utils PUBLIC pouch)

endif(CONFIG_ZCBOR_UTILS)
