zephyr_library()

zephyr_library_sources(
    info_encode.c
    info_characteristic.c
    packetizer.c
    peripheral.c
    uplink_characteristic.c
)

zephyr_linker_sources(DATA_SECTIONS attributes.ld)
zephyr_linker_sources(SECTIONS characteristics.ld)

add_custom_command(OUTPUT info_encode.c
    COMMAND zcbor code -c ${CMAKE_CURRENT_LIST_DIR}/info.cddl -t
    golioth_ble_gatt_info -e --include-prefix cddl/ --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c --output-h ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_encode.h
    BYPRODUCTS include/cddl/info_encode.h include/cddl/info_encode_types.h
    DEPENDS info.cddl)

zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
