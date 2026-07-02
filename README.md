# `pouch`

> [!CAUTION]
> `pouch` is under active development and breaking changes may be
> introduced at any time.

`pouch` is a transport-agnostic application layer protocol that enables
secure and efficient transmission of data between intermittently offline
nodes across multiple network hops. Communication over `pouch` is always
between a **Device** and a **Broker**. A single physical device may act
as a _gateway_ for other devices by implementing both device and broker
support.

## Platforms and Transports

`pouch` is portable across platforms and transports. The following
tables describe device and broker transport support across platforms.

**Device**

| Transport       | Zephyr | ESP-IDF |
|-----------------|--------|---------|
| BLE GATT Server | ✅     |         |
| HTTP Client     | ✅     | ✅      |

**Broker**

| Transport       | Zephyr | ESP-IDF |
|-----------------|--------|---------|
| BLE GATT Client | ✅     |         |


Documentation for each supported platform is provided below. While
`pouch` may work with other platform versions, only those that are
continuously tested and verified are listed.

### Zephyr

The `pouch` repository is a [Zephyr
module](https://docs.zephyrproject.org/latest/develop/modules.html) and can be
included in any Zephyr project by adding the following to the project's
`west.yml` file.

```yaml
- name: pouch
  path: modules/lib/pouch
  revision: main
  url: https://github.com/golioth/pouch.git
```

`pouch` depends on the [`zcbor`](https://github.com/NordicSemiconductor/zcbor)
Python tooling. Use the following command to install it in your environment.

```
pip install -r modules/lib/pouch/requirements.txt
```

See the [Zephyr examples](./examples/zephyr) for more information.

**Supported Versions**

- `v4.4.0`

### nRF Connect SDK (NCS)

`pouch` can also be used with the [nRF Connect SDK
(NCS)](https://github.com/nrfconnect/sdk-nrf), a downstream fork of
Zephyr, using the same steps described above.

**Supported Versions**

- `v3.4.0-rc2`

### ESP-IDF

The `pouch` repository includes multiple [ESP-IDF
components](https://docs.espressif.com/projects/idf-component-manager/en/latest/).
See the [ESP-IDF examples](./examples/esp_idf) for more information on
how to include them in an application.

**Supported Versions**

- `v6.0.1`

## Service SDKs

While `pouch` can be used directly in the same way that any network
protocol can be used directly, SDKs are commonly built on top of `pouch`
to present a higher-level interface to applications. SDKs built on top
of `pouch` are able to leverage all of the supported underlying
transports.

Most SDKs are external to the `pouch` repository and maintain dedicated
documentation and examples. In-tree service SDKs are documented below.

### Golioth Service SDK

The [`golioth_sdk`](./golioth_sdk) implements Golioth device management, data
routing, and application services over `pouch`. The table below indicates the
current status of support for each device-facing service.

| Service                       | Supported |
|-------------------------------|-----------|
| Logging                       | ✅        |
| OTA                           | ✅        |
| Remote Procedure Calls (RPCs) |           |
| Settings                      | ✅        |
| State                         |           |
| Stream                        | ✅        |
