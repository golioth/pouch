# `pouch`

> [!CAUTION]
> `pouch` is under active development and breaking changes may be introduced at
> any time.

`pouch` is a non-IP protocol for communication between devices and cloud
services, typically through one or more gateways.

## Adding `pouch` to a Zephyr Project

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

## Supported Transports

`pouch` messages ("pouches") are delivered over a transport. Transport
implementations can be found in the [`src/transport`](./src/transport)
directory. Currently, implementations for the following transports are provided
in this repository.

### `ble_gatt`

The `ble_gatt` transport delivers pouches over Bluetooth Low Energy (BLE)
Generic Attribute (GATT) Profile. An example of using `pouch` with the
`ble_gatt` transport can be found in the
[`examples/ble_gatt`](./examples/ble_gatt) directory.

## Golioth Service Support

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
