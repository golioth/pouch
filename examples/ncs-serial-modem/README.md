# NCS Serial Modem application

This application builds the [Nordic Connect SDK Serial
Modem](https://github.com/nrfconnect/ncs-serial-modem) firmware for
nRF91 chips. It is used together with the
[`gateway`](../gateway) application on Nordic platforms that combine an
nRF91 cellular modem with a separate Bluetooth-capable chip:

- **Thingy:91 X** — serial modem on nRF9151, gateway on nRF5340
- **nRF9160 DK** — serial modem on nRF9160, gateway on nRF52840

The serial modem firmware runs on the nRF91 chip and exposes LTE
connectivity to the gateway over UART, using the `nordic,nrf91-slm`
driver on the gateway side.

## Building and flashing

This application uses a different west manifest
([`west.yml`](./west.yml)) than the rest of the repository, because it
imports the upstream `ncs-serial-modem` project tree.

Switch to the serial modem manifest and update:

```
west config manifest.file examples/ncs-serial-modem/west.yml
west update
west patch apply
```

Then build and flash for your target:

```
# Thingy:91 X
west build -p -b thingy91x/nrf9151/ns pouch/examples/ncs-serial-modem/
west flash

# nRF9160 DK
west build -p -b nrf9160dk/nrf9160/ns pouch/examples/ncs-serial-modem/
west flash
```

Remember to position the `SWD` selection switch to `nRF91` before
flashing (`SW2` on Thingy:91 X, `SW10` on nRF9160 DK).

After flashing, switch back to the NCS manifest to build the
[`gateway`](../gateway) firmware for the Bluetooth-capable chip:

```
west config manifest.file west-ncs.yml
west update
west patch apply
```

See [`examples/gateway/README.md`](../gateway/README.md#gateway-on-nordic-nrf91-platforms)
for the full end-to-end flow.
