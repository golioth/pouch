zephyr_library()

zephyr_library_sources(bt/bond.c)
zephyr_library_sources(bt/connect.c)
zephyr_library_sources(bt/device_cert.c)
zephyr_library_sources(bt/downlink.c)
zephyr_library_sources(bt/info.c)
zephyr_library_sources(bt/scan.c)
zephyr_library_sources(bt/server_cert.c)
zephyr_library_sources(bt/uplink.c)
zephyr_library_sources(block.c)
zephyr_library_sources(cert.c)
zephyr_library_sources(downlink.c)
zephyr_library_sources(info.c)
zephyr_library_sources(info_decode.c)
zephyr_library_sources(uplink.c)

zephyr_library_link_libraries(mbedTLS)

zephyr_library_get_current_dir_lib_name(${ZEPHYR_BASE} lib_name)

get_filename_component(server_cert_pem "${CONFIG_POUCH_GATEWAY_SERVER_CERT_PEM}" REALPATH BASE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}")

generate_inc_file_for_target(${lib_name}
  ${server_cert_pem}
  ${ZEPHYR_BINARY_DIR}/include/generated/pouch_gateway_server.pem.inc)

add_custom_command(OUTPUT info_decode.c
    COMMAND zcbor code
      -c ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/info.cddl
      -s
      -t pouch_gatt_info
      -d --include-prefix cddl/
      --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_decode.c
      --output-h ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_decode.h
    BYPRODUCTS include/cddl/info_decode.h include/cddl/info_decode_types.h
    DEPENDS ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/info.cddl)

zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
