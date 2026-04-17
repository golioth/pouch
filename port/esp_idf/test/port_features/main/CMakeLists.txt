file(GLOB TEST_SRCS "tests/*.c")

idf_component_register(SRCS "main.c"
                            "other_test_functions.c"
                            "../../../../freertos/freertos_port_layer.c"
                            ${TEST_SRCS}

                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../.."                # ESP-IDF port
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../../include"     # pouch/port.h
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../../../include"  # Pouch includes
                       INCLUDE_DIRS "tests/include"                            # Unity tests

                       LDFRAGMENTS linker.lf)
