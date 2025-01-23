if(NOT CONFIG_POUCH)
    return()
endif()

zephyr_include_directories(include)

zephyr_library()

zephyr_library_sources(
  src/pouch.c
  src/uplink.c
  src/packet.c
  src/block.c
)

zephyr_library_sources_ifdef(CONFIG_POUCH_ENCRYPTION_NONE src/encrypt_none.c)

zephyr_linker_sources(SECTIONS src/event_handlers.ld)
