# Zephry HTTP Client Pytest

This directory contains local pytest-based HIL tests for the
`examples/zephyr/http_client` sample.

## Prerequisites

1. Activate ESP-IDF environment:

```bash
. /path/to/esp-idf/export.sh
```

2. Install pinned Python dependencies into the active Python env:

```bash
uv pip install -r requirements-ci-zephyr.txt --require-hashes
```

3. Install cloud tooling used by the test:

```bash
uv pip install "golioth@git+https://github.com/golioth/python-golioth-tools@v0.8.1"
```

## Build Update Firmware

Note: if you prefer to skip the fw_update test you can ignore this
section and simple add the following to the twister command in place of
fw_update specific flags:

```
--pytest-args="--ota-mode=disabled"
```

While twister will build and flash the device firmware, to test
fw_update you must build the update binary your self and supply the
binary and version pytest args when calling twister.

First, you must update the version number in the VERSION file:
```
# Update to your preferred version, like 2.0.0
nano examples/zephyr/http_client/VERSION
```

The sample.yml sets the package name, you must use the same value. For
instance, the frdm_rw612 package name is set to
`hil_ota_frdm_rw612_http_client`. Here's an example for building and
staging the firmware:

```
# Build the update with specialized configuration
west build -p -b frdm_rw612 --sysbuild examples/zephyr/http_client --     \
  -DCONFIG_EXAMPLE_HTTP_CLIENT_SYNC_PERIOD_S=10                           \
  -DCONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN=64                            \
  -DCONFIG_GOLIOTH_OTA_MAX_VERSION_LEN=64                                 \
  -DCONFIG_EXAMPLE_FW_UPDATE_COMPONENT=\"hil_ota_frdm_rw612_http_client\"

# Copy the signed binary to a known location
cp build/http_client/zephyr/zephyr.signed.bin /tmp/frdm_rw612_update_2.0.0.bin

# Restore the VERSION file so defaults are used by twister
git restore examples/zephyr/http_client/VERSION
```

## Run Test

Set required environment variables:

```bash
export GOLIOTH_API_URL="https://api.golioth.io"
export GOLIOTH_API_KEY="your_project_api_key"
```

Then run from the repository root (`pouch`):

```bash
west twister -vv -W -T examples/zephyr/http_client                       \
        --device-testing --device-serial /dev/ttyACM0                    \
        --west-flash="--dev-id=001063461944"                             \
        -p frdm_rw612                                                    \
        --pytest-args="--generate-certs"                                 \
        --pytest-args="--fw-update-bin=/tmp/frdm_rw612_update_2.0.0.bin" \
        --pytest-args="--fw-update-ver=2.0.0"
```
