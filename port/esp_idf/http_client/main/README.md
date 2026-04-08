# Pouch HTTP Client Example (ESP-IDF)

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
