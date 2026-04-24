# Pouch HTTP Client Example (ESP-IDF)

## Build

### Position Golioth Device PKI

To authenticate with Golioth, place the following files in the
`http_client` directory:

- device.crt.pem
- device.key.pem

The path and names of these files may be adjusted in the configuration
menu (`idf.py menuconfig`).

In the `Example: Pouch HTTP Client` menu:

* Set the Golioth Credentials.
    * Set `Device crt file location (DER format)`.
    * Set `Device key file location (DER format)`.

### Set Target

```
idf.py set-target esp32s3
```

### WiFi Provisioning

Open the project configuration menu (`idf.py menuconfig`).

In the `Example: Pouch HTTP Client` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

Optional: If you need, change the other options according to your
requirements.

### Build, Flash, and Monitor the Project

```
idf.py build flash monitor
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
