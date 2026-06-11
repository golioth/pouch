# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

set(_pouch_src_root ${CMAKE_CURRENT_LIST_DIR})
set(_pouch_target ${POUCH_ACTIVE_TARGET})
set(_pouch_generated_dir ${POUCH_ACTIVE_GENERATED_DIR})

target_sources(${_pouch_target} PRIVATE
    ${_pouch_src_root}/pouch.c
    ${_pouch_src_root}/uplink.c
    ${_pouch_src_root}/header.c
    ${_pouch_src_root}/buf.c
    ${_pouch_src_root}/block.c
    ${_pouch_src_root}/entry.c
    ${_pouch_src_root}/stream.c
    ${_pouch_src_root}/downlink.c
)

pouch_config_enabled(_pouch_encryption_none CONFIG_POUCH_ENCRYPTION_NONE)
if(_pouch_encryption_none)
    target_sources(${_pouch_target} PRIVATE ${_pouch_src_root}/crypto_none.c)
endif()

pouch_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
if(_pouch_encryption_saead)
    target_sources(${_pouch_target} PRIVATE
        ${_pouch_src_root}/crypto_saead.c
        ${_pouch_src_root}/cert.c
        ${_pouch_src_root}/saead/session.c
        ${_pouch_src_root}/saead/uplink.c
        ${_pouch_src_root}/saead/downlink.c
    )
endif()

file(MAKE_DIRECTORY ${_pouch_generated_dir}/include)

target_sources(${_pouch_target} PRIVATE
    ${_pouch_generated_dir}/header_decode.c
    ${_pouch_generated_dir}/header_encode.c
)

target_include_directories(${_pouch_target} PRIVATE ${_pouch_generated_dir}/include)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    add_custom_command(
        OUTPUT
            ${_pouch_generated_dir}/header_decode.c
            ${_pouch_generated_dir}/header_encode.c
        COMMAND zcbor code -c ${_pouch_src_root}/header.cddl -t pouch_header -sde
            --include-prefix cddl/ --oc header.c --oh include/cddl/header.h
        BYPRODUCTS
            ${_pouch_generated_dir}/include/cddl/header_decode.h
            ${_pouch_generated_dir}/include/cddl/header_encode.h
            ${_pouch_generated_dir}/include/cddl/header_types.h
        DEPENDS ${_pouch_src_root}/header.cddl
        WORKING_DIRECTORY ${_pouch_generated_dir})

    string(REPLACE "::" "_" _pouch_codegen_target ${_pouch_target})
    set(_pouch_codegen_target "${_pouch_codegen_target}_generate_headers")
    add_custom_target(${_pouch_codegen_target}
        DEPENDS
            ${_pouch_generated_dir}/header_decode.c
            ${_pouch_generated_dir}/header_encode.c)
    add_dependencies(${_pouch_target} ${_pouch_codegen_target})
endif()


add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/transport transport_common)
