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

## Build Firmware

From the repository root (`pouch`):

```bash
idf.py -C examples/esp_idf/http_client set-target esp32s3
export SDKCONFIG_DEFAULTS="sdkconfig.defaults;pytest/sdkconfig.pytest.defaults"
idf.py -C examples/esp_idf/http_client build
```

This build command includes an overlay file that resets the sync
interval to 10s. This may be omitted to use the default 30s period.

## Run Test

Set required environment variables (test auto-provisions before cloud
checks):

```bash
export GOLIOTH_API_URL="https://api.golioth.io"
export GOLIOTH_API_KEY="your_project_api_key"
export WIFI_SSID="your_wifi_ssid"
export WIFI_PSK="your_wifi_psk"

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
- `--wifi-ssid` (fallback: `WIFI_SSID`)
- `--wifi-psk` (fallback: `WIFI_PSK`)

To generate device certificates automatically during the test run,
pass `--generate-certs` and omit manual cert inputs
(`DEVICE_CRT_DER_PATH`/`DEVICE_KEY_DER_PATH`).

Then run from the repository root (`pouch`):

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
  examples/esp_idf/http_client/pytest/test_sample.py
```

Example generated-cert run:

```bash
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
  examples/esp_idf/http_client/pytest/test_sample.py
```
