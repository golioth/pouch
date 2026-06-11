# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
add_library(zcbor)
target_link_libraries(${COMPONENT_LIB} PUBLIC zcbor)

set(ZCBOR_DIR "${CMAKE_CURRENT_LIST_DIR}/zcbor_submodule")
set(ZCBOR_COMMON_C "${ZCBOR_DIR}/src/zcbor_common.c")

if(NOT EXISTS "${ZCBOR_COMMON_C}")
    message(FATAL_ERROR
        "Missing zcbor submodule source: ${ZCBOR_COMMON_C}\n"
        "The zcbor git submodule appears to be uninitialized.\n"
        "Run:\n"
        "  git submodule update --init --recursive"
    )
endif()

target_include_directories(zcbor PUBLIC "${ZCBOR_DIR}/include")

target_sources(zcbor PRIVATE
        "${ZCBOR_DIR}/src/zcbor_common.c"
        "${ZCBOR_DIR}/src/zcbor_decode.c"
        "${ZCBOR_DIR}/src/zcbor_encode.c"
)

# These mappings adapted from the Zephyr-centric approach:
# https://github.com/zephyrproject-rtos/zephyr/blob/v4.3.0/modules/zcbor/CMakeLists.txt
if(CONFIG_ZCBOR_CANONICAL)
    target_compile_definitions(zcbor PRIVATE ZCBOR_CANONICAL)
endif(CONFIG_ZCBOR_CANONICAL)

if(CONFIG_ZCBOR_STOP_ON_ERROR)
    target_compile_definitions(zcbor PRIVATE ZCBOR_STOP_ON_ERROR)
endif(CONFIG_ZCBOR_STOP_ON_ERROR)

if(CONFIG_ZCBOR_BIG_ENDIAN)
    target_compile_definitions(zcbor PRIVATE ZCBOR_BIG_ENDIAN)
endif(CONFIG_ZCBOR_BIG_ENDIAN)

target_compile_options(zcbor PRIVATE
    "-Wno-unused-but-set-variable"
)
