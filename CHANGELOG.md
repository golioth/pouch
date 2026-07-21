# Changelog

All notable changes to Pouch will be documented in this file.

## [v0.2.0] - 2026-07-23

### Highlights

- **Gateway integrated** — Gateway code brought into the Pouch repository
  and rewritten on the CoAP transport stack.
- **ESP-IDF support** — Pouch now runs on ESP-IDF v6.0.1 with BLE GATT
  and HTTP client transports.
- **Zephyr / NCS updates** — Zephyr bumped to 4.4.0, nRF Connect SDK
  bumped to v3.4-branch tip.
- **Port abstraction layer** — Cross-platform abstractions (mutex,
  semaphore, work queues, atomic ops, logging) decoupled from Zephyr,
  enabling ESP-IDF and future ports.
- **CoAP transport** — New CoAP transport layer for Zephyr; the gateway
  and coap_client sample are built on it.
- **Build system** — Consolidated to standard CMake.

### Added

#### Gateway
- All Gateway code brought into the Pouch repository and substantially
  rewritten.
- Gateway uses the CoAP transport stack with a cloud transport vtable.
- nRF9160-DK `nrf9160` target replaced by nRF9160-DK `nrf52840`.
- Thingy:91X `nrf9151` target replaced by Thingy:91X `nrf5340`.
- `ncs-serial-modem` application for nRF91 Serial LTE Modem integration.
- Unit tests for gateway cloud/uplink/downlink modules.

#### New Examples
- `esp_idf/http_client` — HTTP transport client with runtime provisioning
  and OTA.
- `esp_idf/ble_gatt` — BLE GATT peripheral client with runtime
  provisioning and OTA.
- `zephyr/http_client` — HTTP transport client with native_sim support
  and OTA.
- `zephyr/coap_client` — CoAP transport client with DTLS support.
- `zephyr/ble_gatt` — BLE GATT peripheral client (refactored from
  monolithic app).
- `zephyr/gateway` — Multi-platform gateway application.
- `zephyr/mcumgr_shell`, `zephyr/mcumgr_echo`, `zephyr/serial_comm` —
  MCUmgr and serial samples.

#### Port Abstraction Layer
- Cross-platform port abstractions: mutex, semaphore, work queue, atomic
  operations, logging, singly-linked lists, message queues, iterable
  sections, big endian, delayable work, compile-time assertions.
- Implemented **ESP-IDF** port at v6.0.1
- Add timepoint support.

#### Transport Layer
- Zephyr: CoAP transport
- Zephyr: HTTP client transport
- ESP-IDF: BLE GATT transport
- ESP-IDF: HTTP client transport
- BLE GATT: SAR (Segmentation and Reassembly) sender and receiver
  modules.
- Sliding window acknowledgements for GATT transport.
- `POUCH_TRANSPORT_SAR` Kconfig option.
- `POUCH_TRANSPORT_NONE` choice for unit testing.

#### CI/CD
- `native_parallel` runner for parallel native simulator tests.
- `pytest-pouch` shared pytest plugin.
- Pinned container images and GitHub Actions to digest SHAs.
- CI requirement hashes pinned for reproducibility.
- `encryption_mock` used for testing.
- ESP-IDF HIL (Hardware-in-the-Loop) test workflow.
- ESP-IDF unit test workflow with JUnit report aggregation.

### Changed

- **Zephyr** bumped to 4.4.0;
- **nRF Connect SDK** bumped to v3.4-branch tip.
- Consolidated build system using standard CMake
- Moved Zephyr transports into `port/zephyr/transport/`.

### Fixed

- Downlink: fix `pouch_buf` memory leak.
- Uplink: null pointer check in `pouch_uplink_finish()`.
- Downlink: allocate decode buffer before popping stored block from
  queue.
- Downlink: flush decrypt work before ending crypto session.
- Downlink: free stored `encrypted_block` after `buf_alloc` returns NULL.
- Bufview: all byte accesses made safe (out-of-bounds guards).
- SAEAD: check for active session before attempting decrypt.
- Blockbuf: properly initialize buffers on allocation.
- GATT transport: enforce Bluetooth Secure Connections; add passkey
  confirmation.
- OTA: reject overlong component versions; guard against component name
  overflow.

### Removed

- `encryption_none` replaced by `encryption_mock`.
- Unused `mock_bearer.c` in transport tests.
