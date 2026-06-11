# Copyright (c) 2026 Golioth, Inc.
# SPDX-License-Identifier: Apache-2.0

# native_parallel.cmake - sysbuild helpers for the native_parallel runner.
#
# Application setup
# -----------------
# Each application image (server and client in a sysbuild project)
# needs to override the native_sim board default and register the
# runner in its CMakeLists.txt:
#
#   set(BOARD_FLASH_RUNNER native_parallel)
#   set(BOARD_DEBUG_RUNNER native_parallel)
#   macro(app_set_runner_args)
#     get_property(_np_done GLOBAL PROPERTY _NATIVE_PARALLEL_RUNNER_FINALIZED)
#     if(NOT _np_done)
#       set_property(GLOBAL PROPERTY _NATIVE_PARALLEL_RUNNER_FINALIZED TRUE)
#       board_finalize_runner_args(native_parallel)
#     endif()
#   endmacro()
#
# native_sim's board.cmake calls board_set_flasher_ifnset(native), so
# pre-setting BOARD_FLASH_RUNNER / BOARD_DEBUG_RUNNER is required to
# prevent the defaults from winning.  The app_set_runner_args() hook
# registers native_parallel during board_finalize_runner_args()
# processing.
#
# Sysbuild setup
# --------------
# Include this from your sysbuild.cmake:
#
#   include(${ZEPHYR_POUCH_MODULE_DIR}/cmake/native_parallel.cmake)
#
# Then declare serial links between domains:
#
#   native_parallel_serial_link(
#       ${DEFAULT_IMAGE} uart_1
#       client            uart_1
#   )
#
# At build time this generates a ``native_parallel.yaml`` file in the
# sysbuild build directory.  The ``native_parallel`` west runner reads
# it at ``west flash`` / ``west debug`` time to create socat virtual
# null-modem pairs and wire each domain's UART to the right end.

function(_native_parallel_init)
  get_property(_done GLOBAL PROPERTY _NATIVE_PARALLEL_INITIALIZED)
  if(NOT _done)
    file(WRITE "${CMAKE_BINARY_DIR}/native_parallel.yaml" "links:\n")
    set_property(GLOBAL PROPERTY _NATIVE_PARALLEL_INITIALIZED TRUE)
  endif()
endfunction()

# native_parallel_serial_link(<domain_a> <uart_a> <domain_b> <uart_b>)
#
# Declare a serial link: <domain_a>/<uart_a> <-> <domain_b>/<uart_b>.
#
# <uart_a> / <uart_b> are DTS node names (e.g. ``uart_1``).  The runner
# maps them to the native_sim CLI flag ``-<uart>_port=<path>``.
#
# Adds a flash dependency so that <domain_b> flashes first and
# <domain_a> ends up last in flash_order (the foreground domain).
# west debug reuses flash_order, so the same ordering applies there;
# no separate DEBUG dependency is needed.
function(native_parallel_serial_link domain_a uart_a domain_b uart_b)
  _native_parallel_init()

  file(APPEND "${CMAKE_BINARY_DIR}/native_parallel.yaml"
    "  - a: {domain: ${domain_a}, uart: ${uart_a}}\n"
    "    b: {domain: ${domain_b}, uart: ${uart_b}}\n"
  )

  sysbuild_add_dependencies(FLASH ${domain_a} ${domain_b})
endfunction()
