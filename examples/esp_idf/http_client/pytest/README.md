# ESP-IDF HTTP Client Pytest

This directory contains local pytest-based HIL tests for the
`examples/esp_idf/http_client` sample.

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

   - `examples/esp_idf/http_client/pytest/json-sensor-path-to-lightdb-stream.txt`

Set required environment variables (test auto-provisions before cloud
checks):

```bash
export GOLIOTH_API_URL="https://api.golioth.io"
export GOLIOTH_API_KEY="your_project_api_key"
export WIFI_SSID="your_wifi_ssid"
export WIFI_PSK="your_wifi_psk"
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

1. Build the update firmware with a unique version and OTA package name:

```bash
# Create fw_update.defaults with OTA package name and version
cat > examples/esp_idf/http_client/fw_update.defaults <<EOF
CONFIG_EXAMPLE_FW_UPDATE_COMPONENT="hil_ota_esp32s3_http_client"
CONFIG_APP_PROJECT_VER="$(git rev-parse --short HEAD)"
EOF

idf.py -C examples/esp_idf/http_client set-target esp32s3
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;pytest/sdkconfig.pytest.defaults;fw_update.defaults"
rm -f examples/esp_idf/http_client/sdkconfig
idf.py -C examples/esp_idf/http_client reconfigure
idf.py -C examples/esp_idf/http_client build

# Copy the update binary to a known location
cp examples/esp_idf/http_client/build/pouch_http_client_example.bin /tmp/http_client_esp32s3_fwupdate.bin
```

2. Build the initial version of the example:

```bash
# Create fw_update.defaults with OTA package name (no version override)
cat > examples/esp_idf/http_client/fw_update.defaults <<EOF
CONFIG_EXAMPLE_FW_UPDATE_COMPONENT="hil_ota_esp32s3_http_client"
EOF

export SDKCONFIG_DEFAULTS="sdkconfig.defaults;pytest/sdkconfig.pytest.defaults;fw_update.defaults"
rm -f examples/esp_idf/http_client/sdkconfig
idf.py -C examples/esp_idf/http_client reconfigure
idf.py -C examples/esp_idf/http_client build
```

## Run Test

From the repository root (`pouch`):

```bash
# Full erase to remove existing credentials:
idf.py -C examples/esp_idf/http_client -p /dev/ttyUSB0 erase-flash

# Run pytest
pytest -vv -rs -s \
  -c examples/esp_idf/http_client/pytest.ini \
  --rootdir examples/esp_idf/http_client \
  --embedded-services esp,idf \
  --target esp32s3 \
  --port /dev/ttyUSB0 \
  --app-path examples/esp_idf/http_client \
  --build-dir build \
  --erase-all n \
  --generate-certs \
  --fw-update-pkg-name hil_ota_esp32s3_http_client \
  --fw-update-bin /tmp/http_client_esp32s3_fwupdate.bin \
  --fw-update-ver "$(git rev-parse --short HEAD)" \
  examples/esp_idf/http_client/pytest/test_sample.py
```
