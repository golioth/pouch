# Pouch BLE GATT Example

The Pouch BLE GATT example demonstrates how to create a Pouch application based
on the BLE GATT transport.

## Building

The example should be built with west:

```bash
west build -b <board>
```

The `<board>` should be the Zephyr board ID of your board. The example is
primarily developed and tested on the `nrf52840dk/nrf52840` board, but any Zephyr
board with BLE, PSA, MbedTLS and LittleFS support should work.

## Authentication

The Pouch BLE GATT example requires a private key and certificate to authenticate
and encrypt the communication with the Golioth Cloud.

Pouch devices use the same certificates and keys as devices that connect directly
to the Golioth Cloud. Please refer to [the official
documentation](https://docs.golioth.io/firmware/golioth-firmware-sdk/authentication/certificate-auth)
for generating and signing a valid private key and certificate.

The example's credential management is implemented in src/credentials.c, and
expects to find the following files in its filesystem when booting up:

- A DER encoded certificate at `/lfs1/credentials/crt.der`
- A DER encoded private key at `/lfs1/credentials/key.der`

The `/lfs1/credentials/` directory gets created automatically when the device
boots for the first time.


> [!NOTE]
> The first time the application boots up after being erased, it has to format
> the file system, which generates the following warnings in the device log:
>
> ```log
> [00:00:00.392,486] <err> littlefs: WEST_TOPDIR/deps/modules/fs/littlefs/lfs.c:1389: Corrupted dir pair at {0x0, 0x1}
> [00:00:00.392,517] <wrn> littlefs: can't mount (LFS -84); formatting
> ```
>
> These are expected, and can safely be ignored.

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

`$CERT_FILE` and `$KEY_FILE` are the paths to the DER encoded certificate and
private key files, respectively.

After both files have been transferred, restart the device to initialize Pouch
with the credentials.

### Provisioning with SMP Manager:

[SMP Manager](https://github.com/intercreate/smpmgr) is a Python based SMP client
that can be used as an alternative to MCUmgr. Follow the installation
instructions in the SMP Manager repository, then transfer your certificate and
private key to the device using the following commands:

```bash
smpmgr --port $SERIAL_PORT --mtu 128 file upload $CERT_FILE /lfs1/credentials/crt.der
smpmgr --port $SERIAL_PORT --mtu 128 file upload $KEY_FILE /lfs1/credentials/key.der
```

where `$SERIAL_PORT` is the serial port for the device, like `/dev/ttyACM0` on
Linux, or `COM1` on Windows.

`$CERT_FILE` and `$KEY_FILE` are the paths to the DER encoded certificate and
private key files, respectively.

After both files have been transferred, restart the device to initialize Pouch
with the credentials.
