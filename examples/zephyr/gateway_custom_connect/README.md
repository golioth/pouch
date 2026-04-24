# Pouch Gateway application with custom connect logic

This example demonstrates how to create custom application that uses
Pouch Gateway library and implements custom logic around Bluetooth
scanning, device selection and connection management.

During scanning Bluetooth peripherals need to follow specific criteria
in order to initiate connection to them:
- advertise Pouch Service UUID (0xFC49) with compatible version and
  "sync request" flag set
- use "Golioth" as advertised name
- have RSSI higher than -70 dBm

Bluetooth connection is maintained for two Pouch synchonization events,
with a delay of 5 seconds between them:
- scan
- connect
- Pouch sync
- delay 5s
- Pouch sync
- disconnect

## Building and flashing

The example should be built with west:

```bash
$ west build -b <board> samples/custom_connect
$ west flash
```

## Provisioning

```sh
uart:~$ settings set golioth/psk-id <my-psk-id@my-project>
uart:~$ settings set golioth/psk <my-psk>
uart:-$ kernel reboot
```
