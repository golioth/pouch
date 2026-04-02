idf_component_register(SRCS "main.c"
                            "other_test_functions.c"
                            "test_mutex"
                            "../../../freertos/freertos_port_layer.c"

                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/.."                # ESP-IDF port
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../include"     # pouch/port.h
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../../include"  # Pouch includes

                       LDFRAGMENTS ../../linker.lf)
