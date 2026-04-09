# Copyright (c) 2025 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

file(GLOB children RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*)

foreach(child ${children})
if(IS_DIRECTORY ${curdir}/${child})
    include("${CMAKE_CURRENT_LIST_DIR}/${child}/${chile}.cmake")
endif()
endforeach()
