# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(IDF_TARGET STREQUAL "linux")
    # Ensure we build for linux as a 32-bit application
    # or the atomic port will assert during build
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -march=i686")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -march=i686")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
endif()

list(APPEND EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/tests
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(esp_idf_unit_test)
