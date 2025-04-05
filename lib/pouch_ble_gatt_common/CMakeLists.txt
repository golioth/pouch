if(NOT CONFIG_POUCH_TRANSPORT_BLE_GATT_COMMON)
    return()
endif()

zephyr_include_directories(include)

zephyr_library()

zephyr_library_sources(
    packetizer.c
)
