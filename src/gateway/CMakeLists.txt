zephyr_library()

zephyr_library_sources(
    block.c
    cert.c
    downlink.c
    info.c
    info_decode.c
    uplink.c
)
zephyr_library_sources(
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/endpoints/gateway/info.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/endpoints/gateway/uplink.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/endpoints/gateway/downlink.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/endpoints/gateway/device_cert.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/endpoints/gateway/server_cert.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/sar/packet.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/sar/receiver.c
    ${ZEPHYR_POUCH_MODULE_DIR}/src/transport/sar/sender.c
    ${ZEPHYR_POUCH_MODULE_DIR}/port/zephyr/transport/gatt/gateway/bond.c
    ${ZEPHYR_POUCH_MODULE_DIR}/port/zephyr/transport/gatt/gateway/discover.c
    ${ZEPHYR_POUCH_MODULE_DIR}/port/zephyr/transport/gatt/gateway/gateway.c
    ${ZEPHYR_POUCH_MODULE_DIR}/port/zephyr/transport/gatt/gateway/scan.c
)
zephyr_library_include_directories(${ZEPHYR_POUCH_MODULE_DIR}/src)

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
