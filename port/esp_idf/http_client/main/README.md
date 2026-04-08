# Pouch HTTP Client Example (ESP-IDF)

## Position Golioth Device PKI

To authenticate with Golioth, a place the following files in the
`http_client/main` directory:

- device.crt.pem
- device.key.pem

## Set Target

```
idf.py set-target esp32s3
```

## WiFi Provisioning

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Configuration` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

Optional: If you need, change the other options according to your requirements.

## Build, Flash, and Monitor the Project

```
idf.py build flash monitor
```
