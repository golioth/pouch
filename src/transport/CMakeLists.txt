# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

pouch_config_enabled(_pouch_encryption_saead CONFIG_POUCH_ENCRYPTION_SAEAD)
pouch_config_enabled(_pouch_transport_sar CONFIG_POUCH_TRANSPORT_SAR)
pouch_config_enabled(_pouch_transport_ble_gatt CONFIG_POUCH_TRANSPORT_BLE_GATT)
pouch_config_enabled(_pouch_transport_ble_gatt_broker CONFIG_POUCH_GATEWAY_GATT)
pouch_config_enabled(_pouch_gateway CONFIG_POUCH_GATEWAY)
pouch_config_enabled(_pouch_transport_serial CONFIG_POUCH_TRANSPORT_SERIAL)
pouch_config_enabled(_pouch_transport_serial_broker CONFIG_POUCH_TRANSPORT_SERIAL_BROKER)
pouch_config_enabled(_pouch_transport_serial_device CONFIG_POUCH_TRANSPORT_SERIAL_DEVICE)

# The device-side endpoints are only used by the transports that speak the
# endpoint protocol: the GATT peripheral and the serial device. Builds without
# either (e.g. CONFIG_POUCH_TRANSPORT_NONE) must not pull them in - they depend
# on Kconfig symbols that only exist alongside a real transport.
if(_pouch_transport_ble_gatt OR _pouch_transport_serial_device)
    set(_pouch_device TRUE)
else()
    set(_pouch_device FALSE)
endif()

if(NOT _pouch_device
   AND NOT _pouch_gateway
   AND NOT _pouch_transport_sar
   AND NOT _pouch_transport_serial)
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
if(_pouch_gateway)
    target_sources(${_pouch_target} PRIVATE
        ${_pouch_transport_root}/endpoints/broker/info.c
        ${_pouch_transport_root}/endpoints/broker/uplink.c
        ${_pouch_transport_root}/endpoints/broker/downlink.c
        ${_pouch_transport_root}/endpoints/broker/device_cert.c
        ${_pouch_transport_root}/endpoints/broker/server_cert.c
    )
endif()

if(_pouch_device)
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

if(_pouch_transport_serial)
    target_sources(${_pouch_target} PRIVATE
        ${_pouch_transport_root}/serial/channel.c
        ${_pouch_transport_root}/serial/packet.c
        ${_pouch_transport_root}/serial/serial.c
    )

    if (_pouch_transport_serial_broker)
        target_sources(${_pouch_target} PRIVATE
            ${_pouch_transport_root}/serial/broker.c
        )
    endif()

    if (_pouch_transport_serial_device)
        target_sources(${_pouch_target} PRIVATE
            serial/device.c
        )
    endif()
endif()
