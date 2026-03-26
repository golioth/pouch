# Copyright (c) 2025 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED
  HINTS $ENV{ZEPHYR_BASE}
  COMPONENTS
    python
    extensions
    basic_settings
    west
    yaml
    zephyr_module
    boards
    hwm_v2
    soc
)
project(bsim_bin LANGUAGES)

add_custom_target(runners_yaml_props_target)

set(PROJECT_BINARY_DIR ${APPLICATION_BINARY_DIR}/zephyr)

foreach(dir ${BOARD_DIRECTORIES})
  include(${dir}/board.cmake OPTIONAL)
endforeach()

add_subdirectory(${ZEPHYR_BASE}/cmake/flash flash)

set(gen_dts_edt ${ZEPHYR_BASE}/scripts/dts/gen_edt.py)
execute_process(
    COMMAND ${PYTHON_EXECUTABLE} ${gen_dts_edt}
    --dts ${CMAKE_CURRENT_SOURCE_DIR}/empty.dts
    --dtc-flags ""
    --bindings-dirs ${CMAKE_CURRENT_SOURCE_DIR}
    --dts-out ${CMAKE_CURRENT_BINARY_DIR}/zephyr/zephyr.dts
    --edt-pickle-out ${CMAKE_CURRENT_BINARY_DIR}/zephyr/edt.pickle
)
