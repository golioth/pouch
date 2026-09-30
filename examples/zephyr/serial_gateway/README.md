# Pouch SPI Serial Gateway application

This application is using the Pouch Gateway library and implements the
default logic around the SPI serial transport.

Unlike the Bluetooth-based [gateway](../gateway) example, which scans for
advertising Pouch peripherals and establishes short-lived connections to
synchronize them, the SPI serial gateway communicates with a single Pouch
device over a dedicated SPI bus. There is no discovery, scanning or connection
management: the gateway and device are wired together on the same SPI bus, and
the device requests a sync through its data-ready line.

Apart from the transport it uses to reach the device, the gateway is the same
as the Bluetooth gateway example, and shares its credential and network
handling. The gateway firmware is responsible for:

- establishing its own connection to Golioth over CoAP (on Ethernet or WiFi),
- running the Pouch gateway services (certificate and uplink modules),
- driving the SPI bus as the SPI master through the `pouch-broker` device.

The SPI master is configured through a board-specific devicetree overlay (see
`boards/frdm_rw612.overlay` for reference). The overlay declares a
`pouch-broker` child node under the SPI controller that binds to the
`golioth,pouch-broker` compatible, providing the CS, data-ready and
`spi-max-frequency` properties used by the transport. A board overlay replaces
`app.overlay`, so it must include `app.overlay` to keep the file system that
holds the credentials.

The frame size (`CONFIG_POUCH_SPI_BROKER_FRAME_SIZE`) must match the device's
`CONFIG_POUCH_SPI_DEVICE_FRAME_SIZE`.

## Building and flashing

The example should be built with west:

```bash
$ west build -b <board> serial_gateway
$ west flash
```

## Provisioning

The gateway is provisioned the same way as the Bluetooth gateway: see the
[gateway provisioning instructions](../gateway/README.md#provisioning).

## WiFi Gateway Using the NXP frdm_rw612

By default the frdm_rw612 will build with Ethernet support, but may
instead be built with WiFi support:

```sh
$ west build -p -b frdm_rw612 serial_gateway -- -DEXTRA_CONF_FILE=boards/frdm_rw612_wifi.conf
$ west flash
```

Use the shell to provision WiFi credentials:

```sh
uart:~$ wifi cred add -s <your-wifi-ssid> -p <your-wifi-password> -k 1
uart:~$ wifi cred auto_connect
```
