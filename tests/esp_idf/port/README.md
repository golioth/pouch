# Unit Tests for Pouch ESP-IDF Port

Run unit tests for the Pouch port functions used by ESP-IDF. This covers
both the ESP-IDF and FreeRTOS port implementations.

## Run on ESP32s3

```
idf.py set-target esp32s3
idf.py build flash monitor
```

## Run on Linux

### Install 32-bit Dependencies for Debian-based Distros

Linux builds may be used to test this port, but they must be built as a
32-bit application to match the requirements of the FreeRTOS Atomic
types.

```
sudo dpkg --add-architecture i386
sudo apt-get update

sudo apt-get install -y      \
              gcc-multilib   \
              g++-multilib   \
              libc6-dev-i386 \
              libc6-dev:i386 \
              libbsd-dev:i386
```

### Build and Run Unit Tests

```
idf.py --preview set-target linux
idf.py build
./build/esp_idf_unit_test.elf
```

### Linux Caveats

* The Linux simulation doesn't run `ESP_SYSTEM_INIT_FN` so anything that
  uses `POUCH_APPLICATION_STARTUP_HOOK` needs to be called manually
  before a test runs.
* The Linux simulation doesn't support multiple cores. Any tests that
  target more than one ESP32 core should be ignored (see
  `test_automic_concurrency()`).
