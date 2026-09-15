# Gateway Credentials

The declared gateway and gateway_custom_connect scenarios mount `../creds`
from each image's build directory. The default `creds_dir` fixture writes to
`<sysbuild build directory>/creds`, matching those mounts. The shared CA is
registered once per test module and removed from the project at teardown.
Device PEM keys, CSRs, certificates, and signing serial files are temporary;
only the configured device DER files are published alongside the shared CA.

`SB_CONFIG_GATEWAY_MOUNT_CREDS` and `SB_CONFIG_PERIPHERAL_MOUNT_CREDS` are
boolean enable switches, disabled by default for manual builds. The scenarios
enable both; the host path is fixed at `../creds`, mounted at `/creds` in
Zephyr. Keep the shared `creds` directory beside the image build directories.

Gateway-only settings runs register the CA and generate gateway credentials
without requiring `peripheral_ble_gatt_example_0`. If that image is present,
its identity is generated using its configured DER filenames. OTA package
discovery still requires the peripheral's build configuration when requested
through `fw_update_package`; it is not an autouse dependency.

## OTA Integrity

`pouch.gateway.ota` uses the shared dummy OTA size of 400 KiB
(`400 * 1024 = 409600` bytes), matching the direct-client tests. It downloads
the image through the gateway over BLE and compares the peripheral's SHA256
with the uploaded artifact's digest. This checks multi-block integrity, not
deterministic backpressure or a firmware reboot.

The shared digest wait is 180 seconds; the Twister scenario timeout is
300 seconds, including fixture setup and teardown. The larger scenario cap
does not extend the digest wait.
