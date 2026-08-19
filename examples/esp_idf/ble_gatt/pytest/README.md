# ESP-IDF BLE GATT Pytest

This directory contains local pytest-based HIL tests for the
`examples/esp_idf/ble_gatt` sample.

You must have a gateway device provisioned and running to complete these
tests. See: examples/zephyr/gateway.

## Prerequisites

1. Activate ESP-IDF environment:

```bash
. /path/to/esp-idf/export.sh
```

2. Install pinned Python dependencies into the active ESP-IDF Python
   env:

```bash
uv pip install --python "$IDF_PYTHON_ENV_PATH" \
    -r requirements-ci-esp-idf.txt --require-hashes
```

3. Install cloud tooling used by the test:

```bash
uv pip install --python "$IDF_PYTHON_ENV_PATH" \
    "golioth@git+https://github.com/golioth/python-golioth-tools@v0.8.1"
```

Set required environment variables:

```bash
export GOLIOTH_API_URL="https://api.golioth.io"
export GOLIOTH_API_KEY="your_project_api_key"
```

## Build Update Firmware

Note: if you prefer to skip the fw_update test you can ignore this
section and simply add the following to the pytest command in place of
fw_update specific flags:

```
--ota-mode=disabled
```

OTA testing requires two firmware builds: an update version (unique
version number) and an initial version (default version).

Build the update firmware with a unique version and OTA package name:

```bash
# Create fw_update.defaults with OTA package name and version
cat > examples/esp_idf/ble_gatt/fw_update.defaults <<EOF
CONFIG_EXAMPLE_FW_UPDATE_COMPONENT="hil_ota_esp32s3_ble_gatt"
CONFIG_APP_PROJECT_VER="$(git rev-parse --short HEAD)"
EOF

idf.py -C examples/esp_idf/ble_gatt set-target esp32s3
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;pytest/sdkconfig.pytest.defaults;fw_update.defaults"
rm -f examples/esp_idf/ble_gatt/sdkconfig
idf.py -C examples/esp_idf/ble_gatt reconfigure
idf.py -C examples/esp_idf/ble_gatt build

# Copy the update binary to a known location
cp examples/esp_idf/ble_gatt/build/pouch_ble_gatt_example.bin /tmp/ble_gatt_esp32s3_fwupdate.bin
```

## Build Initial Version of Firmware

Build the initial version of the firmware (defaults to 1.0.0) with a
unique version and OTA package name:

```bash
# Create fw_update.defaults with OTA package name (no version override)
cat > examples/esp_idf/ble_gatt/fw_update.defaults <<EOF
CONFIG_EXAMPLE_FW_UPDATE_COMPONENT="hil_ota_esp32s3_ble_gatt"
EOF

idf.py -C examples/esp_idf/ble_gatt set-target esp32s3
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;pytest/sdkconfig.pytest.defaults;fw_update.defaults"
rm -f examples/esp_idf/ble_gatt/sdkconfig
idf.py -C examples/esp_idf/ble_gatt reconfigure
idf.py -C examples/esp_idf/ble_gatt build
```

## Run Test

Run all tests:

```bash
# Full erase to remove existing credentials:
idf.py -C examples/esp_idf/ble_gatt -p /dev/ttyUSB0 erase-flash

# Run pytest
pytest -vv -rs -s \
  -c examples/esp_idf/ble_gatt/pytest.ini \
  --rootdir examples/esp_idf/ble_gatt \
  --embedded-services esp,idf \
  --target esp32s3 \
  --port /dev/ttyUSB0 \
  --app-path examples/esp_idf/ble_gatt \
  --build-dir build \
  --erase-all n \
  --generate-certs \
  --fw-update-pkg-name hil_ota_esp32s3_ble_gatt \
  --fw-update-bin /tmp/ble_gatt_esp32s3_fwupdate.bin \
  --fw-update-ver "$(git rev-parse --short HEAD)" \
  examples/esp_idf/ble_gatt/pytest/test_sample.py
```
