# Pouch rpmsg Gateway

The Linux half of Pouch on an MPU+MCU SoC. The MCU runs Zephyr and the Pouch
device stack (see `examples/zephyr/rpmsg_device`); this daemon runs on the
application processor, brokers the MCU's sync sessions to a Pouch-compatible
server, and speaks the Pouch Serial protocol over an rpmsg endpoint.

```
┌──────────── Linux (Cortex-A) ─────────────┐   ┌──── Zephyr (Cortex-M) ────┐
│  cloud ◀── HTTPS ── pouch-rpmsg-gateway   │   │  app + Pouch device stack │
│                          │                │   │            │              │
│                          └─ /dev/rpmsgN ──┼───┼─▶ rpmsg transport adapter │
└───────────────────────────────────────────┘   └───────────────────────────┘
              virtio vrings in shared memory (rpmsg)
```

The MCU keeps its own cloud identity: the session is encrypted end to end
between it and the cloud, so this process forwards ciphertext and never holds a
device key. It authenticates upstream with its **own** certificate, which is a
separate identity from the device's.

## Building

```sh
go build ./cmd/pouch-rpmsg-gateway

# or for the target, which needs no cgo:
GOOS=linux GOARCH=arm64 CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" \
    -o pouch-rpmsg-gateway ./cmd/pouch-rpmsg-gateway
```

## Running

Load and start the MCU firmware first, then attach:

```sh
rproc=$(grep -l '^imx-rproc$' /sys/class/remoteproc/*/name | xargs dirname)
cp zephyr.elf /lib/firmware/
echo zephyr.elf > $rproc/firmware
echo start > $rproc/state

pouch-rpmsg-gateway \
    -device /dev/rpmsg0 \
    -cloud https://gw.golioth.io \
    -cert gateway.crt.der -key gateway.key.der
```

`-once` runs a single session and exits, which is the easiest way to see what is
happening; `-v` adds per-channel logging. Certificates may be DER or PEM.

The device announces its endpoint through the rpmsg name service. With the
default name `rpmsg-raw` the kernel's `rpmsg_char` driver binds it and it shows
up as `/dev/rpmsgN` with no `RPMSG_CREATE_EPT_IOCTL` needed.

## What a session does

The broker drives a fixed verb sequence, skipping whatever is already done:

| Channel | Direction | Purpose |
|---------|-----------|---------|
| 0 INFO | device → gateway | what the device already holds |
| 1 SERVER_CERT | gateway → device | the server chain, so the device can trust the cloud |
| 2 DEVICE_CERT | device → gateway | the device's leaf, forwarded to `/.g/device-cert` |
| 3 DOWNLINK | gateway → device | the cloud's reply |
| 4 UPLINK | device → gateway | the outbound pouch, posted to `/.g/pouch` |

INFO carries a flags byte and the serial of the server certificate the device
holds. If that serial matches the chain we would push, the SERVER_CERT phase is
skipped; if the flags say the cloud already has the device's certificate, so is
DEVICE_CERT.

## Layout

| Path | Purpose |
|------|---------|
| `internal/serial` | The Pouch Serial protocol: header codec, channel state machine, broker verb loop |
| `internal/link` | Frame I/O over an rpmsg character device |
| `internal/cloud` | The `/.g/*` HTTP contract and upstream mTLS |
| `internal/gateway` | Session wiring: endpoints, provisioning decisions, the pump |
| `cmd/pouch-rpmsg-gateway` | The daemon |

`internal/serial` is a port of `src/transport/serial/` from this repo. Its tests
drive a device built from the same state machine, mirroring
`tests/pouch/serial/exchange`, and the header vectors are bytes captured from a
real device so the two implementations are pinned to each other rather than only
to themselves.

## Notes for anyone writing another gateway

Two things here are not obvious and cost real debugging time:

- **Write to the rpmsg device with blocking writes.** `internal/link` uses raw
  file descriptors rather than `*os.File` on purpose. Go registers pollable
  descriptors with its netpoller and makes them non-blocking, and
  `virtio_rpmsg_poll()` reports the endpoint writable only when a TX buffer is
  already free — it never enables the tx-complete callback, because only the
  blocking `write(2)` path calls `rpmsg_upref_sleepers()`. A non-blocking writer
  that drains the ring then waits for a wakeup that cannot arrive. This bites any
  runtime that makes descriptors non-blocking by default (Go, Node, async Rust).

- **The downlink cannot be fetched until the uplink is complete.** Both
  directions open in the same phase, so the device usually prompts for its
  downlink before it has finished sending the uplink. There is nothing to post
  at that point, and an empty body is not a valid pouch — the server answers
  400. The downlink sender reports `MoreData` with zero bytes until the uplink
  transfer closes, which re-arms the channel without putting a frame on the wire.
