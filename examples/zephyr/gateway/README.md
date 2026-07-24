# Pouch Gateway application

This application is using Pouch Gateway library and implements default
logic around Bluetooth scanning, device selection and connection
management.

During scanning Bluetooth peripherals need to follow specific criteria
in order to initiate connection to them:

- advertise Pouch Service UUID (0xFC49 or
  89a316ae-89b7-4ef6-b1d3-5c9a6e27d272 for backward compatibility) with
  compatible version and "sync request" flag set

Bluetooth connection is maintained just for the time Pouch
synchronizatio takes place:

- scan
- connect
- Pouch sync
- disconnect

## Building and flashing

The example should be built with west:

```bash
$ west build -b <board> gateway
$ west flash
```

## Provisioning

The example is set up with
[MCUmgr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html)
support for transferring credentials into the device's built-in file system over
a serial connection.

### Provisioning with MCUmgr:

[The MCUmgr CLI](https://github.com/apache/mynewt-mcumgr) is available as a
command line tool built as a Go package. Follow the installation instructions in
the MCUmgr repository, then transfer your certificate and private key to the
device using the following commands:

```bash
mcumgr --conntype serial --connstring $SERIAL_PORT fs upload $CERT_FILE /lfs1/credentials/crt.der
mcumgr --conntype serial --connstring $SERIAL_PORT fs upload $KEY_FILE /lfs1/credentials/key.der
```

where `$SERIAL_PORT` is the serial port for the device, like `/dev/ttyACM0` on
Linux, or `COM1` on Windows.

### Provisioning with littlefs binary

Credentials can also be provisioned by creating a LittleFS binary image
containing the certificate and key files, then flashing it directly to the
storage partition on the device.

This approach is useful when MCUmgr USB/serial provisioning is not available,
such as on platforms where the UART is used for other purposes (e.g. serial
modem communication on a Thingy91x-based gateway).

Install the `littlefs_tools` Python package:

```bash
pip install littlefs_tools
```

Create a directory structure for the credentials:

```bash
mkdir -p littlefs/credentials
```

Place your DER-encoded certificate and private key files into the
`littlefs/credentials/` directory:

```
littlefs/
└── credentials
    ├── crt.der
    └── key.der
```

Generate the LittleFS binary image:

```bash
littlefs create -s littlefs -i image.bin -b 4096 -c 8
```

Convert the binary to Intel HEX format at the appropriate flash offset for the
target platform:

```bash
arm-zephyr-eabi-objcopy -I binary -O ihex --change-addresses <storage-partition-address> image.bin lfs_storage.hex
```

The `<storage-partition-address>` is the flash address of the `storage_partition`
in the device tree. See the platform-specific examples below.

Flash the HEX file to the device:

```bash
nrfutil device program \
        --firmware lfs_storage.hex \
        --core application \
        --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_SYSTEM,verify=VERIFY_READ
```

#### Thingy:91 X

For the Thingy:91 X, the nRF5340 application core has the storage partition
at address `0xF8000`. Use `arm-zephyr-eabi-objcopy` from the Zephyr SDK to
convert the binary:

```bash
/path/to/zephyr-sdk-0.17.2/arm-zephyr-eabi/bin/arm-zephyr-eabi-objcopy \
  -I binary -O ihex --change-addresses 0xF8000 image.bin lfs_storage.hex
```

Ensure the SWD switch (`SW2`) is set to `nRF53`, then flash to the nRF5340
application core:

```bash
nrfutil device program \
        --firmware lfs_storage.hex \
        --core application \
        --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,reset=RESET_SYSTEM,verify=VERIFY_READ
```

## WiFi Gateway Using the NXP frdm_rw612

By default the frdm_rw612 will build with Ethernet support, but may
instead be built with WiFi support:

```sh
$ west build -p -b frdm_rw612 gateway -- -DEXTRA_CONF_FILE=boards/frdm_rw612_wifi.conf
$ west flash
```

Use the shell to provision WiFi credentials:

```sh
uart:~$ wifi cred add -s <your-wifi-ssid> -p <your-wifi-password> -k 1
uart:~$ wifi cred auto_connect
```

## Gateway on Nordic nRF91 platforms

On Nordic platforms that combine an nRF91 cellular modem with a
separate Bluetooth-capable chip (nRF52840 or nRF5340), the gateway
firmware is split across two chips:

- **Serial modem firmware** runs on the nRF91 chip and provides LTE
  connectivity to the gateway over UART. See
  [`examples/zephyr/ncs-serial-modem`](../ncs-serial-modem) for the
  serial modem application.
- **Gateway firmware** runs on the Bluetooth-capable chip (nRF52840 on
  nRF9160 DK, nRF5340 on Thingy:91 X). It runs the Bluetooth Host
  stack and communicates with the nRF91 serial modem over UART for
  Internet access.

Both firmwares must be flashed for the platform to work.

### Thingy:91 X

Serial modem firmware runs on the nRF9151 chip. Switch to the serial
modem manifest, build and flash by changing the `SWD` switch (`SW2`)
to `nRF91`, then:

```
west config manifest.file examples/zephyr/ncs-serial-modem/west.yml
west update
west patch apply
west build -p -b thingy91x/nrf9151/ns pouch/examples/zephyr/ncs-serial-modem/
west flash
```

Gateway firmware runs on the nRF5340 chip. Switch back to the NCS
manifest, build and flash by changing the `SWD` switch (`SW2`) to
`nRF53`, then:

```
west config manifest.file west-ncs.yml
west update
west patch apply
west build -p -b thingy91x/nrf5340/cpuapp --sysbuild pouch/examples/zephyr/gateway/
west flash
```

### nRF9160 DK

Serial modem firmware runs on the nRF9160 chip. Switch to the serial
modem manifest, build and flash by changing the `SWD` switch (`SW10`)
to `nRF91`, then:

```
west config manifest.file examples/zephyr/ncs-serial-modem/west.yml
west update
west patch apply
west build -p -b nrf9160dk/nrf9160/ns pouch/examples/zephyr/ncs-serial-modem/
west flash
```

Gateway firmware runs on the nRF52840 chip. Switch back to the NCS
manifest, build and flash by changing the `SWD` switch (`SW10`) to
`nRF52`, then:

```
west config manifest.file west-ncs.yml
west update
west patch apply
west build -p -b nrf9160dk/nrf52840 --sysbuild pouch/examples/zephyr/gateway/
west flash
```
