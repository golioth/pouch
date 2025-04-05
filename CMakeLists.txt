if(CONFIG_POUCH)

    zephyr_include_directories(include)

    zephyr_library()

    zephyr_library_sources(
      ${CMAKE_CURRENT_BINARY_DIR}/header_encode.c
      src/pouch.c
      src/uplink.c
      src/header.c
      src/buf.c
      src/block.c
      src/entry.c
      src/stream.c
    )

    zephyr_library_sources_ifdef(CONFIG_POUCH_ENCRYPTION_NONE src/crypto_none.c)

    zephyr_linker_sources(SECTIONS src/event_handlers.ld)

    add_custom_command(OUTPUT header_encode.c
        COMMAND zcbor code -c ${CMAKE_CURRENT_LIST_DIR}/src/header.cddl -t pouch_header -se --include-prefix cddl/ --oc header_encode.c --oh include/cddl/header_encode.h
        BYPRODUCTS include/cddl/header_encode.h include/cddl/header_types.h
        DEPENDS src/header.cddl)

    zephyr_library_include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

    # Transports

    if (CONFIG_POUCH_TRANSPORT_BLE_GATT)
        add_subdirectory(src/transport/ble_gatt)
    endif()

endif()

# Libraries

add_subdirectory(lib)
