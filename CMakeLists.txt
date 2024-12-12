if(NOT CONFIG_TOOTHFAIRY)
    return()
endif()

zephyr_include_directories(include)

zephyr_library()

zephyr_library_sources(
    info_encode.c
    src/info_characteristic.c
    src/packetizer.c
    src/peripheral.c
    src/uplink_characteristic.c
)

zephyr_linker_sources(DATA_SECTIONS src/attributes.ld)
zephyr_linker_sources(SECTIONS src/characteristics.ld)

add_custom_command(OUTPUT info_encode.c
    COMMAND zcbor code -c ${CMAKE_CURRENT_LIST_DIR}/src/info.cddl -t tf_info -e --include-prefix cddl/ --output-c ${CMAKE_CURRENT_BINARY_DIR}/info_encode.c --output-h ${CMAKE_CURRENT_BINARY_DIR}/include/cddl/info_encode.h
    BYPRODUCTS include/cddl/info_encode.h include/cddl/info_encode_types.h
    DEPENDS src/info.cddl)

zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
