# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

pouch_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
pouch_config_enabled(_pouch_transport_sar CONFIG_POUCH_TRANSPORT_SAR)
pouch_config_enabled(_pouch_transport_ble_gatt CONFIG_POUCH_TRANSPORT_BLE_GATT)

if(NOT _pouch_transport_sar AND NOT _pouch_transport_ble_gatt)
    return()
endif()

set(_pouch_transport_root ${CMAKE_CURRENT_LIST_DIR})
set(_pouch_target ${POUCH_ACTIVE_TARGET})
set(_pouch_generated_dir ${POUCH_ACTIVE_GENERATED_DIR})

if(_pouch_transport_sar)
    target_sources(${_pouch_target} PRIVATE
        ${_pouch_transport_root}/sar/sender.c
        ${_pouch_transport_root}/sar/receiver.c
        ${_pouch_transport_root}/sar/packet.c
    )
endif()

file(MAKE_DIRECTORY ${_pouch_generated_dir}/include)

target_include_directories(${_pouch_target} PRIVATE
    ${_pouch_transport_root}
    ${_pouch_generated_dir}/include
)

if(_pouch_transport_ble_gatt)
    target_sources(${_pouch_target} PRIVATE
        ${_pouch_transport_root}/endpoints/device/info.c
        ${_pouch_transport_root}/endpoints/device/uplink.c
        ${_pouch_transport_root}/endpoints/device/downlink.c
        ${_pouch_generated_dir}/info_encode.c
    )

    if(_pouch_encryption_saead)
        target_sources(${_pouch_target} PRIVATE
            ${_pouch_transport_root}/endpoints/device/device_cert.c
            ${_pouch_transport_root}/endpoints/device/server_cert.c
        )
    endif()

    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        add_custom_command(
            OUTPUT ${_pouch_generated_dir}/info_encode.c
            COMMAND zcbor code -c ${_pouch_transport_root}/info.cddl -t pouch_gatt_info -e
                --include-prefix cddl/ --output-c ${_pouch_generated_dir}/info_encode.c
                --output-h ${_pouch_generated_dir}/include/cddl/info_encode.h
            BYPRODUCTS
                ${_pouch_generated_dir}/include/cddl/info_encode.h
                ${_pouch_generated_dir}/include/cddl/info_encode_types.h
            DEPENDS ${_pouch_transport_root}/info.cddl
            WORKING_DIRECTORY ${_pouch_generated_dir}
        )

        string(REPLACE "::" "_" _pouch_transport_codegen_target ${_pouch_target})
        set(_pouch_transport_codegen_target "${_pouch_transport_codegen_target}_generate_transport_info")
        add_custom_target(${_pouch_transport_codegen_target}
            DEPENDS ${_pouch_generated_dir}/info_encode.c)
        add_dependencies(${_pouch_target} ${_pouch_transport_codegen_target})
    endif()
endif()
