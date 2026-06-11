# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)      # pouch/port.h
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)         # Zephyr port includes
zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/zephyr_port_layer.c) # Port functions

set(POUCH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

if(CONFIG_POUCH)

    zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include) # Pouch includes

    zephyr_library_named(pouch)
    set(POUCH_ACTIVE_TARGET ${ZEPHYR_CURRENT_LIBRARY})

    zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_LIST_DIR}/downlink_handlers.ld)
    zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_LIST_DIR}/uplink_handlers.ld)
    zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_LIST_DIR}/event_handlers.ld)

    # Golioth_SDK linker sources
    include(${CMAKE_CURRENT_LIST_DIR}/../../golioth_sdk/cmake/golioth_sdk_linker_files.cmake)
    golioth_sdk_get_linker_files_no_ext(_golioth_sdk_linker_files_no_extension)
    foreach(_golioth_sdk_linker_file IN LISTS _golioth_sdk_linker_files_no_extension)
        zephyr_linker_sources(SECTIONS ${_golioth_sdk_linker_file}.ld)
    endforeach()

    if (DEFINED CONFIG_POUCH_CA_CERT_FILENAME)
        find_file(ca_cert ${CONFIG_POUCH_CA_CERT_FILENAME}
            PATHS
            ${APP_DIR}
            ${POUCH_ROOT}
            REQUIRED)

        generate_inc_file_for_target(${ZEPHYR_CURRENT_LIBRARY}
            ${ca_cert}
            ${ZEPHYR_BINARY_DIR}/include/generated/pouch_ca_cert.inc)

        zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/pouch_ca_cert.c)
    endif()

    set(POUCH_ACTIVE_TARGET ${ZEPHYR_CURRENT_LIBRARY})
    set(POUCH_ACTIVE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR})

    # Check for zcbor python package
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} -c "import zcbor"
        RESULT_VARIABLE _pouch_zcbor_check
        OUTPUT_QUIET ERROR_QUIET)
    if(_pouch_zcbor_check)
        get_filename_component(_pouch_requirements_txt "${POUCH_ROOT}/requirements.txt" ABSOLUTE)
        message(FATAL_ERROR
            "\n\nThe 'zcbor' Python package is required by pouch but was not found.\n"
            "Install it with:\n"
            "  pip install -r ${_pouch_requirements_txt}\n\n")
    endif()

    zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/blockbuf.c)

    zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
    zephyr_library_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../src)

    zephyr_library_link_libraries(mbedTLS)

    # Transports

    if (CONFIG_POUCH_TRANSPORT_BLE_GATT)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/transport/gatt transport_gatt)
    endif()
    if (CONFIG_POUCH_TRANSPORT_HTTP_CLIENT)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/transport/http transport_http)
    endif()
    if (CONFIG_POUCH_TRANSPORT_COAP_CLIENT)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/transport/coap transport_coap)
    endif()
endif()

if(CONFIG_POUCH_GATEWAY)
    zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/gateway gateway)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/transport/gatt/broker gatt_broker)
endif()
