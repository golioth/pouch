# Pouch rpmsg Device Example

Demonstrates a Pouch device on the MCU half of a heterogeneous MPU+MCU SoC. The
MCU runs Zephyr and the Pouch device stack; a broker on the Linux side forwards
its pouches to the cloud over OpenAMP/rpmsg — the standard on-die IPC on these
parts. The MCU keeps its own cloud identity and end-to-end encryption, so the
Linux side only ever handles ciphertext.

The transport is an adapter on the Pouch Serial core rather than a transport of
its own: one rpmsg message carries one Pouch Serial frame, with no segmentation.
The same device code runs over a plain UART, which is what the `native_sim`
build uses so the sample can be exercised without hardware.

## Building

### Native Simulator (UART link)

`native_sim` builds the UART adapter. `uart0` stays the console and `uart1`
carries the Pouch link on its own pty, which a broker process attaches to.

```sh
west build -b native_sim examples/zephyr/rpmsg_device
west build -t run
```

The pty for `uart1` is printed on startup, e.g.
`UART_1 connected to pseudotty: /dev/pts/6`.

### i.MX95 EVK, Cortex-M7 (rpmsg link)

```sh
west build -b imx95_evk/mimx9596/m7 examples/zephyr/rpmsg_device
```

`boards/imx95_evk_mimx9596_m7.conf` selects the resource-table adapter in place
of the UART one (`CONFIG_POUCH_RPMSG_RSC_DEVICE=y`), and the overlay points
`zephyr,ipc_shm` / `zephyr,ipc_rsc_table` at the reserved-memory carve-outs in
NXP's Linux device tree.

> [!NOTE]
> `CONFIG_POUCH_RPMSG_RSC_DEVICE` builds its vdev from the **resource table**, which is
> what the Linux kernel's `virtio_rpmsg_bus` expects. Zephyr's `ipc_service` backends
> use a static vring layout Linux does not understand, so they are not an option here.

## Running against Linux

The Linux side loads and starts this firmware through `remoteproc`. Find the
node by name rather than assuming an index — a board may expose several, and on
the i.MX95 FRDM `remoteproc0` is the Neutron NPU while the M7 is `remoteproc1`:

```sh
rproc=$(grep -l '^imx-rproc$' /sys/class/remoteproc/*/name | xargs dirname)

cp zephyr.elf /lib/firmware/
echo zephyr.elf > $rproc/firmware
echo start > $rproc/state
```

The device announces its endpoint through the rpmsg name service under the name
in `CONFIG_POUCH_RPMSG_RSC_DEVICE_EPT_NAME`. The default, `rpmsg-raw`, is what
binds the kernel's `rpmsg_char` driver, so the endpoint appears to userspace as
`/dev/rpmsgN` with no `RPMSG_CREATE_EPT_IOCTL` needed. The broker reads and
writes Pouch Serial frames on that character device.

> [!CAUTION]
> Write to `/dev/rpmsgN` with **blocking** writes. `virtio_rpmsg_poll()` reports
> the endpoint writable only when a TX buffer is already free and never enables
> the tx-complete callback; only the blocking `write(2)` path arms it. A
> non-blocking writer that drains the ring — routine when streaming — waits for
> a wakeup that cannot arrive. This bites runtimes that make descriptors
> non-blocking by default (Go, Node, async Rust).

## Provisioning

> [!CAUTION]
> This sample embeds a **self-signed placeholder** key and certificate
> (`src/device_key.der.inc`, `src/device_crt.der.inc`) so it builds and runs
> without a provisioned filesystem. They are not secret and are not trusted by
> the cloud. Do not ship them.

A real deployment needs a device certificate signed by a CA the cloud trusts.
See the [Golioth PKI documentation][pki] for issuing one, and the `coap_client`
or `ble_gatt` examples for the filesystem-based provisioning pattern
(`/lfs1/credentials/crt.der` + `key.der`) to use in place of
`src/credentials.c`.

[pki]: https://docs.golioth.io/connectivity/credentials/

## Tuning notes

The board config carries settings that were not obvious and are worth keeping if
you adapt this to another SoC:

- `CONFIG_POUCH_RPMSG_RSC_DEVICE_THREAD_PRIORITY` must be **below** the Pouch
  work queue (`CONFIG_POUCH_THREAD_PRIORITY`, default 5). At equal priority the
  management thread drains the entire receive vring without yielding while the
  decrypt queue never runs, and the downlink heap-allocates until `malloc`
  fails — which puts the downlink channel into a permanent error state.
- `CONFIG_POUCH_RPMSG_RSC_DEVICE_STACK_SIZE` has to cover the whole serial-core
  receive chain, including the server-cert endpoint's 4 KB allocation. 2 KB
  faults partway through the certificate transfer.
- `CONFIG_HEAP_MEM_POOL_SIZE` must cover libmetal/OpenAMP virtqueue state, the
  server-certificate allocation, *and* the encrypted downlink blocks in flight
  at once.
