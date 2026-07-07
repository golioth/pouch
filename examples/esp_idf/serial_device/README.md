# Pouch UART Serial Device Example (ESP-IDF)

This example demonstrates a Pouch device using the UART serial transport to
communicate with a Pouch gateway. The device runs on an ESP32 and exchanges
Pouch data with a gateway over a point-to-point UART link.

Unlike the BLE GATT transport, where the device advertises and waits for a
gateway to connect, the UART serial transport is always-on: the device and
gateway are wired together and communicate as soon as both are running.

## Build

### Set Target

```
idf.py set-target esp32s3
```

### Build, Flash, and Monitor

```
idf.py build flash monitor
```

## UART Configuration

The UART pins and baud rate are configured via Kconfig
(`Component config → Pouch → Pouch UART Transport`). The defaults are:

| Setting | Default |
|---|---|
| UART port | 1 |
| Baud rate | 115200 |
| TX pin | GPIO 17 |
| RX pin | GPIO 18 |

Wire the device's TX to the gateway's RX and vice versa. A common ground
is required.

## Provisioning

This example uses runtime provisioning via the serial console. Credentials
are stored in non-volatile storage (NVS) so they persist across power cycles
and firmware upgrades.

After credentials have been stored, reset the device (e.g. `reset` from the
console) to begin using them.

### Golioth Credentials

Acquire a device certificate and private key in DER format. You may use
the demo certificate generation tool in the
[Golioth console](https://console.golioth.io) or use
[your own PKI](https://docs.golioth.io/connectivity/credentials/pki/).

Add your certificate and key to the device via the serial console. The
credentials must be base64-encoded DER:

```
pouch> crt CRT_der_in_base64_format
pouch> key KEY_der_in_base64_format
pouch> reset
```

Convert DER to base64 using:

```
$ base64 --wrap=0 device.crt.der
```

## Settings

The Golioth Settings service is enabled. A boolean setting named `LED` is
registered. When updated from the
[Golioth console](https://console.golioth.io), the new value is logged.

## OTA Firmware Update

Over-the-Air (OTA) firmware update is enabled. To generate an update binary:

1. Change the application version number in `sdkconfig.defaults`
   (`CONFIG_APP_PROJECT_VER`), then delete `sdkconfig` before rebuilding.
2. Rebuild: `idf.py build`
3. Upload `build/pouch_uart_serial_device_example.bin` to Golioth.
