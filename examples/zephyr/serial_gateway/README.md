# Pouch SPI Serial Gateway application

This application is using the Pouch Gateway library and implements the
default logic around the SPI serial transport.

Unlike the Bluetooth-based gateway, which scans for advertising Pouch
peripherals and establishes short-lived connections to synchronize them, the
SPI serial gateway communicates with a single Pouch device over a dedicated
SPI bus. There is no discovery, scanning or connection management: the
gateway and device are wired together on the same SPI bus and the gateway
polls the device continuously.

The gateway firmware is responsible for:

- establishing its own connection to Golioth (over Ethernet, WiFi or LTE),
- running the Pouch gateway services (certificate, uplink and downlink
  modules),
- driving the SPI bus as the SPI master through the `pouch-broker` device.

The SPI master is configured through a board-specific devicetree overlay
(see `app.overlay` and the `frdm_rw612.overlay` for reference). The overlay
declares a `pouch-broker` child node under the SPI controller that binds to
the `golioth,pouch-broker` compatible, providing the CS, data-ready and
`spi-max-frequency` properties used by the transport.

## Building and flashing

The example should be built with west:

```bash
$ west build -b <board> serial_gateway
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
$ west build -p -b frdm_rw612 serial_gateway -- -DEXTRA_CONF_FILE=boards/frdm_rw612_wifi.conf
$ west flash
```

Use the shell to provision WiFi credentials:

```sh
uart:~$ wifi cred add -s <your-wifi-ssid> -p <your-wifi-password> -k 1
uart:~$ wifi cred auto_connect
```
