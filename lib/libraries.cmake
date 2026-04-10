# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

file(GLOB dir_list RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*)

foreach(dname ${dir_list})
    if(IS_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/${dname})
        include("${CMAKE_CURRENT_LIST_DIR}/${dname}/${dname}.cmake")
    endif()
endforeach()
