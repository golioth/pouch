set_config_string(${DEFAULT_IMAGE} CONFIG_GOLIOTH_COAP_HOST_URI "${SB_CONFIG_GOLIOTH_COAP_HOST_URI}")

if(NOT SB_CONFIG_POUCH_GATEWAY_CLOUD)
  set_config_bool(${DEFAULT_IMAGE} CONFIG_POUCH_GATEWAY_CLOUD n)
endif()

if(BOARD MATCHES "bsim")
  # Override boot banner string for easier identification
  set_config_string(${DEFAULT_IMAGE} CONFIG_BOOT_BANNER_STRING "Booting ${DEFAULT_IMAGE}")

  ExternalZephyrProject_Add(
    APPLICATION bsim_2G4_phy
    SOURCE_DIR ${ZEPHYR_POUCH_MODULE_DIR}/bsim_bin
    BOARD bsim_2G4_phy
  )
  sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} bsim_2G4_phy)

  if(SB_CONFIG_BSIM_HANDBRAKE)
    ExternalZephyrProject_Add(
      APPLICATION bsim_handbrake
      SOURCE_DIR ${ZEPHYR_POUCH_MODULE_DIR}/bsim_bin
      BOARD bsim_device/native/handbrake
    )
    sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} bsim_handbrake)
  endif()

  function(add_peripheral name path)
    string(TOUPPER "${name}" sb_variable_suffix)
    set(sb_variable_enable_name "SB_CONFIG_PERIPHERAL_${sb_variable_suffix}")
    set(sb_variable_num_name "SB_CONFIG_PERIPHERAL_${sb_variable_suffix}_NUM")

    if(${sb_variable_enable_name})
      math(EXPR num_minus_one "${${sb_variable_num_name}} - 1")
      foreach(i RANGE ${num_minus_one})
        set(target_name "peripheral_${name}_${i}")

        ExternalZephyrProject_Add(
          APPLICATION ${target_name}
          SOURCE_DIR ${path}
        )
        sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} ${target_name})

        # Override boot banner string for easier identification
        set_config_string(${target_name} CONFIG_BOOT_BANNER_STRING "Booting ${target_name}")

        # Mount /creds, which need to be generated before running BabbleSim
        if(SB_CONFIG_PERIPHERAL_MOUNT_CREDS)
          set_config_string(${target_name} CONFIG_NATIVE_EXTRA_CMDLINE_ARGS "-volume=creds:/creds")
          set_config_string(${target_name} CONFIG_EXAMPLE_CREDENTIALS_DIR "/creds")
        endif()

        if(name STREQUAL "ble_gatt_example" AND
           SB_CONFIG_GOLIOTH_COAP_HOST_URI STREQUAL "coaps://coap.golioth.dev")
          set_config_string(${target_name} CONFIG_POUCH_SERVER_CERT_CN "pouch.golioth.dev")
        endif()
      endforeach()
    endif()
  endfunction()

  add_peripheral(zephyr
    ${ZEPHYR_BASE}/samples/bluetooth/peripheral)
  add_peripheral(periodic_uplink
    ${ZEPHYR_POUCH_MODULE_DIR}/samples/peripheral/periodic_uplink)
  add_peripheral(ble_gatt_example
    ${ZEPHYR_POUCH_MODULE_DIR}/examples/zephyr/ble_gatt)
endif()
