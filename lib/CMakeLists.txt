# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

file(GLOB lib_lists "${CMAKE_CURRENT_LIST_DIR}/*/CMakeLists.txt")

foreach(list_file ${lib_lists})
  get_filename_component(lib_dir ${list_file} DIRECTORY)

  add_subdirectory(${lib_dir})
endforeach()
