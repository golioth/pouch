if (NOT CONFIG_POUCH)
    return()
endif()

zephyr_include_directories(include)

zephyr_library()

zephyr_library_sources(
    src/uplink.c
)
