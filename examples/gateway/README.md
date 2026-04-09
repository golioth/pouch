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

```sh
uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
uart:~$ settings set golioth/psk <my-psk>
uart:-$ kernel reboot
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
  [`examples/ncs-serial-modem`](../ncs-serial-modem) for the serial
  modem application.
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
west config manifest.file examples/ncs-serial-modem/west.yml
west update
west patch apply
west build -p -b thingy91x/nrf9151/ns pouch/examples/ncs-serial-modem/
west flash
```

Gateway firmware runs on the nRF5340 chip. Switch back to the NCS
manifest, build and flash by changing the `SWD` switch (`SW2`) to
`nRF53`, then:

```
west config manifest.file west-ncs.yml
west update
west patch apply
west build -p -b thingy91x/nrf5340/cpuapp --sysbuild pouch/examples/gateway/
west flash
```

### nRF9160 DK

Serial modem firmware runs on the nRF9160 chip. Switch to the serial
modem manifest, build and flash by changing the `SWD` switch (`SW10`)
to `nRF91`, then:

```
west config manifest.file examples/ncs-serial-modem/west.yml
west update
west patch apply
west build -p -b nrf9160dk/nrf9160/ns pouch/examples/ncs-serial-modem/
west flash
```

Gateway firmware runs on the nRF52840 chip. Switch back to the NCS
manifest, build and flash by changing the `SWD` switch (`SW10`) to
`nRF52`, then:

```
west config manifest.file west-ncs.yml
west update
west patch apply
west build -p -b nrf9160dk/nrf52840 --sysbuild pouch/examples/gateway/
west flash
```

### Flashing pre-built binaries

Pre-built firmware binaries for Thingy:91 X and nRF9160 DK are
available as release artifacts. They can be programmed with the
[`nrfutil`](https://www.nordicsemi.com/Products/Development-tools/nRF-Util)
CLI tool.

<details>

<summary>Flashing the Thingy:91 X</summary>

1. Program the nRF9151 Serial Modem Firmware

    a. Position the SWD selection switch (`SW2`) to `nRF91`

    b. Issue the following command:
    ```
    nrfutil device program --firmware thingy91x_nrf9151.hex --x-family nrf91
    ```

2. Program the nRF5340 Gateway Firmware

    a. Power cycle the device and position the SWD selection switch (`SW2`) to
    `nRF53`

    b. Issue the following command:
    ```
    nrfutil device program --firmware thingy91x_nrf5340.hex --x-family nrf53 --core application
    ```

</details>

<details>

<summary>Flashing the nRF9160 DK</summary>

1. Program the nRF9160 Serial Modem Firmware

    a. Position the SWD selection switch (`SW10`) to `nRF91`

    b. Issue the following command:
    ```
    nrfutil device program --firmware nrf9160dk_nrf9160.hex --x-family nrf91
    ```

2. Program the nRF52840 Gateway Firmware

    a. Power cycle the device and position the SWD selection switch (`SW10`) to
    `nRF52`

    b. Issue the following command:
    ```
    nrfutil device program --firmware nrf9160dk_nrf52840.hex --x-family nrf52
    ```

</details>
