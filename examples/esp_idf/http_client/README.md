# Pouch HTTP Client Example (ESP-IDF)

## Build

The example uses ESP-IDF Component Manager for the `pouch` dependency via
`main/idf_component.yml`.

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

### WiFi Credentials

Use the `ssid` and `psk` console commands to store your WiFi AP
credentials on the device:

```
pouch> ssid your_wifi_ssid
pouch> psk your_wifi_psk
```

Note that you may configure other WiFi settings using `idf.py
menuconfig` by navigating to `Example: Pouch HTTP Client` --> `Wi-Fi
Configuration`.

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

## OTA Firmware Update

Over-the-Air (OTA) firmware update is enabled in this example. To
generate an update binary:

1. Change the application version number (LOG_TIMESTAMP_SOURCE_SYSTEM)
    - Option 1: Using menuconfig: `idf.py menuconfig` Application manager ->
      Project version
    - Option 2: Update the value in `sdkconfig.defaults`, then delete `sdkconfig` before rebuilding.
2. Rebuild the project: `idf.py build`
3. Upload `build/pouch_http_client_example.bin` to Golioth using the
   same package name (default: `main`) and version number as the build.

Ensure your device is part of a Cohort and roll out a new deployment to
that cohort that contains this updated package. See the [Golioth
docs](https://docs.golioth.io/device-management/ota/) for more
information.
