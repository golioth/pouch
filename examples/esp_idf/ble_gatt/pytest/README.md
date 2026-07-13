# ESP-IDF BLE GATT Pytest

This directory contains local pytest-based HIL tests for the
`examples/esp_idf/ble_gatt` sample.

## Prerequisites

1. Activate ESP-IDF environment:

```bash
. /path/to/esp-idf/export.sh
```

2. Install pinned Python dependencies into the active ESP-IDF Python
   env:

```bash
uv pip install --python "$IDF_PYTHON_ENV_PATH" -r requirements-ci-esp-idf.txt --require-hashes
```

3. Install cloud tooling used by the test:

```bash
uv pip install --python "$IDF_PYTHON_ENV_PATH" "golioth@git+https://github.com/golioth/python-golioth-tools@v0.8.1"
```

4. Enable the required Golioth pipeline route for stream validation:

   The stream test (`test_sensor_uplink_contains_temp`) expects JSON data
   sent on `.s/sensor` to be routed into LightDB Stream. Enable the pipeline
   from this file in your Golioth project before running the test:

   - `examples/esp_idf/ble_gatt/pytest/json-sensor-path-to-lightdb-stream.txt`

## Build Firmware

From the repository root (`pouch`):

```bash
idf.py -C examples/esp_idf/ble_gatt set-target esp32s3
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;pytest/sdkconfig.pytest.defaults"
idf.py -C examples/esp_idf/ble_gatt build
```

This build command includes an overlay file that resets the sync
interval to 10s. This may be omitted to use the default 30s period.

## Run Test

Set required environment variables (test auto-provisions before cloud
checks):

```bash
export GOLIOTH_API_URL="https://api.golioth.io"
export GOLIOTH_API_KEY="your_project_api_key"

# Manual cert mode: provide DER file paths
export DEVICE_CRT_DER_PATH="/path/to/device.crt.der"
export DEVICE_KEY_DER_PATH="/path/to/device.key.der"

# Optional override (otherwise derived from cert CN in DEVICE_CRT_DER_PATH)
export GOLIOTH_DEVICE_NAME="your_device_name"
```

The test also accepts `python-golioth-tools`/SDK-style pytest flags:

- `--api-key` (fallback: `GOLIOTH_API_KEY`)
- `--api-url` (fallback: `GOLIOTH_API_URL`, default: `https://api.golioth.io`)
- `--device-name` (fallback: `GOLIOTH_DEVICE_NAME`)

To generate device certificates automatically during the test run,
pass `--generate-certs` and omit manual cert inputs
(`DEVICE_CRT_DER_PATH`/`DEVICE_KEY_DER_PATH`).

Then run from the repository root (`pouch`):

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
  examples/esp_idf/ble_gatt/pytest/test_sample.py
```

Example generated-cert run:

```bash
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
  examples/esp_idf/ble_gatt/pytest/test_sample.py
```
