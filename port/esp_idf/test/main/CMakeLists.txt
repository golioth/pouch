idf_component_register(SRCS "main.c"
                            "other_test_functions.c"
                            "../../../freertos/freertos_port_layer.c"

                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/.."
                       INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/../../../include"

                       LDFRAGMENTS ../../linker.lf)
