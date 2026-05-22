# Pouch BLE GATT Example (ESP-IDF)

## Build

### Set Target

```
idf.py set-target esp32s3
```

### Build, Flash, and Monitor the Project

```
idf.py build flash monitor
```

## Provisioning

This example uses runtime provisioning via the serial console.
Credentials are placed in non-volatile storage (NVS) so they will
persist across power cycles and firmware upgrades.

After credentials have been stored, reset the device (eg: `reset` from
the console) to begin using them.

### Golioth Credentials

To authenticate with Golioth, acquire a device certificate and private
key in DER format. You may use the demo certificate generation tool in
the [Golioth console](https://console.golioth.io) or using [your own PKI
generation](https://docs.golioth.io/connectivity/credentials/pki/).

Add your certificate and key to the device by using the serial console.
The credentials must be base64-encoded DER and are stored by using the
`crt` and `key` commands:

```
pouch> crt CRT_der_in_base64_format
pouch> key KEY_der_in_base64_format
```

Device PKI credentials may be converted to base64 using your preferred
command line tools. For example:

```
$ base64 --wrap=0 device.key.der
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgttpz/RWMuZZlhTeb6RqpFmXPf3ZyE9iR4OxQtw9mpGyhRANCAAQCtBI3OltVfxyZ7l6CmwAP/jvybEmR9HW2gRebtRoVi0MT7o/NOcuxMCR6o00nfcYA3BZWKmpicLE2MZPCs9P6
```
