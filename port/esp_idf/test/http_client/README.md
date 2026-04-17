# Pouch HTTP Client Example (ESP-IDF)

## Position Golioth Device PKI

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

## Set Target

```
idf.py set-target esp32s3
```

## WiFi Provisioning

Open the project configuration menu (`idf.py menuconfig`).

In the `Example: Pouch HTTP Client` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

Optional: If you need, change the other options according to your
requirements.

## Build, Flash, and Monitor the Project

```
idf.py build flash monitor
```
