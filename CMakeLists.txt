if(NOT CONFIG_TOOTHFAIRY)
    return()
endif()

zephyr_include_directories(include)

zephyr_library()

zephyr_library_sources(
    src/info_characteristic.c
    src/packetizer.c
    src/peripheral.c
)

zephyr_linker_sources(DATA_SECTIONS src/attributes.ld)
