# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

golioth_sdk_require_defined_vars(
    _golioth_sdk_root
    _golioth_sdk_srcs
    _golioth_sdk_linker_files_no_extension
    _golioth_sdk
)

if(_golioth_sdk)
    foreach(_golioth_sdk_linker_file IN LISTS _golioth_sdk_linker_files_no_extension)
        list(APPEND _golioth_sdk_linker_fragments ${_golioth_sdk_linker_file}.lf)
    endforeach()

    idf_component_register(SRCS
                           ${_golioth_sdk_srcs}

                           INCLUDE_DIRS
                           ${_golioth_sdk_root}/include

                           LDFRAGMENTS
                           ${_golioth_sdk_linker_fragments}

                           REQUIRES
                           pouch
                          )

    idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
else()
    idf_component_register()
endif()
