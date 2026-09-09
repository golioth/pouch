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
| 5 FW_STATUS | gateway → device | what became of an image we were handed |
| 6 FW | device → gateway | a firmware image relayed through the device |
| 7 TIME | gateway → device | our wall clock, for a device that has none |
| 8 FW_URL | device → gateway | a signed URL for an artifact we should fetch |

INFO carries a flags byte and the serial of the server certificate the device
holds. If that serial matches the chain we would push, the SERVER_CERT phase is
skipped; if the flags say the cloud already has the device's certificate, so is
DEVICE_CERT. A second flag says the device can be handed firmware as a signed
URL; without it we neither push the time nor poll for a URL, so a device built
without those channels is never left waiting on one.

Channels 5 through 8 are all optional. Leaving their endpoints unset leaves the
channels unconfigured, which is what a gateway that does not apply firmware
wants, and the device tolerates that because an unconfigured channel is ignored
rather than dereferenced.

## Firmware updates

The device this gateway serves is loaded from the host filesystem by remoteproc
at every boot, so it has no flash of its own and cannot apply its own update. It
is still the authenticated Pouch endpoint, so the cloud offers updates to it and
it asks us to install one. It can ask in two ways.

**Signed URL** (`-signed-url`). The device signs a URL for its own artifact with
its private key and hands us that. We fetch the image over HTTPS, check it
against the size and SHA-256 the manifest carried, and install it. The signature
is the only credential involved: it covers one artifact, expires in about ninety
seconds, and is not ours — we present no identity of our own to the artifact
host. No image byte crosses the rpmsg link.

Because an artifact takes longer to fetch than a session lasts, the download
runs in the background and its verdict goes out on the next session, one
`-interval` later. The device is waiting for exactly that: it told the cloud to
stop offering the component when it handed the URL over.

Signing needs a clock, and the device has none, so we report ours on the TIME
channel before the certificate phases. **The gateway's own clock therefore has
to be right** — NTP, or every URL the device signs falls outside its validity
window and the cloud refuses it.

**Relay** (`-firmware`). The device downloads the image through Pouch itself and
streams the plaintext to us over the FW channel, and we verify the SHA-256
before installing. It works without any cloud feature, at the cost of moving
every byte twice and holding the whole image in memory here. The device falls
back to this when a signed URL is refused.

Either way, `-firmware-dir` says where a verified image lands and `-remoteproc`
points at the core to restart onto it. The restart happens after the session
ends: the device is owed its verdict first, and taking the core down mid-session
drops the link out from under the exchange still running on it.
`-firmware-reject` reports a hash failure for an image that actually verified,
which is the only convenient way to exercise the device's retry path.

Signed URLs additionally require the feature to be enabled for the Golioth
project, and the CA that issued the device certificate to be uploaded to it. A
403 from the artifact host is what either omission looks like from here.

> [!CAUTION]
> With `-signed-url` this process fetches a URL the device chose. The device is
> the trust boundary — it is on the same die, and it is the only thing holding a
> key — but the URL is still checked rather than trusted: `https` only, bounded
> in length, no redirect off `https`, and the response is read only as far as
> the size the manifest promised.

## Layout

| Path | Purpose |
|------|---------|
| `internal/serial` | The Pouch Serial protocol: header codec, channel state machine, broker verb loop |
| `internal/link` | Frame I/O over an rpmsg character device |
| `internal/cloud` | The `/.g/*` HTTP contract and upstream mTLS |
| `internal/gateway` | Session wiring: endpoints, provisioning decisions, the pump, firmware |
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
