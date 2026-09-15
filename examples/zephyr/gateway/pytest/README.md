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
its identity is generated using its configured DER filenames.
