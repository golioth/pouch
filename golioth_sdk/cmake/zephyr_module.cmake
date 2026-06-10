# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

golioth_sdk_require_defined_vars(
    _golioth_sdk_root
    _golioth_sdk_srcs
    _golioth_sdk_linker_files
    _golioth_sdk
)

if(NOT _golioth_sdk)
    return()
endif()

zephyr_library_named(pouch_golioth_sdk)
zephyr_include_directories(${_golioth_sdk_root}/include)

if(_golioth_sdk_srcs)
    target_sources(pouch_golioth_sdk PRIVATE ${_golioth_sdk_srcs})
endif()

foreach(_golioth_sdk_linker_file IN LISTS _golioth_sdk_linker_files)
    zephyr_linker_sources(SECTIONS ${_golioth_sdk_linker_file})
endforeach()
