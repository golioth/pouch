# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Register the native_parallel runner before Zephyr processes board.cmake.
set(BOARD_FLASH_RUNNER native_parallel)
macro(app_set_runner_args)
  get_property(_np_done GLOBAL PROPERTY _NATIVE_PARALLEL_RUNNER_FINALIZED)
  if(NOT _np_done)
    set_property(GLOBAL PROPERTY _NATIVE_PARALLEL_RUNNER_FINALIZED TRUE)
    board_finalize_runner_args(native_parallel)
  endif()
endmacro()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mcumgr_echo_server)

target_sources(app PRIVATE src/main.c)
