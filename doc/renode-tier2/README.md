<!--
Copyright (c) 2026 Golioth, Inc.
SPDX-License-Identifier: Apache-2.0
-->

# Renode Tier-2: real remoteproc + virtio-rpmsg for the Pouch rpmsg transport

This directory holds the work-in-progress harness for validating the Pouch
**rpmsg (OpenAMP) transport** against a *real* Linux `remoteproc` +
`virtio-rpmsg` stack, with no hardware, using [Renode](https://renode.io).

It complements the in-repo native_sim tests (`tests/pouch/rpmsg/exchange`),
which validate the serial-core protocol + UART framing but cannot exercise the
`ipc_service`/OpenAMP adapter. This tier exercises the actual on-die rpmsg path.

> Status: **work in progress.** The mechanism is validated and the Zephyr R5
> firmware builds and boots; wiring the Pouch device firmware + a mock broker
> into an automated end-to-end test is ongoing. This cannot run in the pouch
> CI (no OpenAMP/ZynqMP there); it is validated locally in Renode.

## Platform: AMD Kria KV260 (ZynqMP), Cortex-R5

Renode ships a ZynqMP OpenAMP demo (`scripts/single-node/zynqmp_openamp.resc`)
that boots Linux on the Cortex-A53 and drives an OpenAMP peer on the Cortex-R5
over real `remoteproc` + `virtio-rpmsg`. Zephyr's in-tree `kv260_r5` board
(SoC `zynqmp_rpu`) targets that same R5, so it is the vehicle here.

RZ/G2L (`rzg2l_openamp.resc` + Zephyr `rzg2l_smarc` cm33) is a second candidate
and an actual RFC target family; ZynqMP was chosen first because its Renode
demo is the most mature.

## What is validated

- **Renode v1.16.1** runs headless (portable, self-contained .NET).
- The shipped **`Should Run OpenAMP Echo Sample`** robot test passes — real
  Linux `remoteproc` (`modprobe zynqmp_r5_remoteproc` → set firmware → `echo
  start`) + `virtio_rpmsg_bus` round-trips payloads with the R5. The mechanism
  works in Renode on this host.
- A Zephyr **`hello_world` built for `kv260_r5`** boots on the emulated R5
  (`rpu0`) — see `smoke_r5_boot.robot`. Console is **uart1** on this board.
- The Zephyr **`openamp_rsc_table`** sample builds for `kv260_r5` using the
  overlay/conf here.
- Linux **`remoteproc` loads and starts** that firmware on the R5
  (`state` → `running`) — see `remoteproc_load.robot`. The firmware is injected
  into a copy of the demo rootfs with `debugfs` (no root needed):

  ```sh
  debugfs -w -R "write build/zephyr/zephyr_openamp_rsc_table.elf \
    /lib/firmware/rpmsg-echo.out" rootfs.ext2
  POUCH_ROOTFS=$PWD/rootfs.ext2 ./renode-test <pouch>/doc/renode-tier2/remoteproc_load.robot
  ```

### Firmware placement note (important)

The ZynqMP R5 **boots and executes from TCM** (reset vector at address `0`), not
from DDR. Firmware must therefore be **TCM-resident** — the known-good
`rpmsg-echo.out` links to `0x0` (ATCM) + `0x20000` (BTCM), ~87 KiB total, with
only its resource table in DDR. A firmware relinked entirely into the DDR
`rproc` carve-out loads (remoteproc reports `running`) but never executes — the
R5 boots into empty TCM and sits silent. Confirmed both ways: a TCM-linked
`hello_world` prints over remoteproc; a DDR-linked one is silent.

Consequently the overlay keeps the default `0x0` (TCM) link address and only
places the vrings/buffers in DDR (`zephyr,ipc_shm` @ `0x3ed40000`). The OpenAMP
sample must be trimmed to fit TCM: with `CONFIG_SHELL=n`, `CONFIG_LOG=n`,
`CONFIG_BOOT_BANNER=n` it drops from ~155 KiB to **~45 KiB**, which fits ATCM as
a single segment. The eventual Pouch device firmware must likewise fit TCM
(~128 KiB across ATCM+BTCM) — a real constraint to design around.

## The OpenAMP memory map (from the demo's Linux DTB)

Decompiled from the demo's device tree, the RPU carve-out is:

| Region                | Address       | Size    |
|-----------------------|---------------|---------|
| R5 firmware (rproc)   | `0x3ed00000`  | 256 KiB |
| vring0                | `0x3ed40000`  | 16 KiB  |
| vring1                | `0x3ed44000`  | 16 KiB  |
| rpmsg buffer pool     | `0x3ed48000`  | 1 MiB   |

The R5 firmware itself runs from TCM (the `kv260_r5` default link address `0x0`);
the shared rings/buffers live in DDR at `0x3ed40000`. The IPI mailbox is the
`rpu0_ipi` node (`xlnx,zynqmp-ipi-mailbox`), driven on the Zephyr side by
`drivers/ipm/ipm_xlnx_ipi.c`.

`kv260_r5-openamp.overlay` / `.conf` encode this: `zephyr,ipc_shm` →
`memory@3ed40000`, `zephyr,ipc` → `&rpu0_ipi`.

## How to run

Install Renode (portable) and its test deps, then:

```sh
# Build the R5 OpenAMP firmware for kv260_r5
export ZEPHYR_SDK_INSTALL_DIR=<sdk>
west build -b kv260_r5 zephyr/samples/subsys/ipc/openamp_rsc_table \
  -- -DEXTRA_DTC_OVERLAY_FILE=$PWD/doc/renode-tier2/kv260_r5-openamp.overlay \
     -DEXTRA_CONF_FILE=$PWD/doc/renode-tier2/kv260_r5-openamp.conf

# Smoke-test that a kv260_r5 Zephyr image boots on the emulated R5
cd <renode-dir>
./renode-test <pouch>/doc/renode-tier2/smoke_r5_boot.robot   # uses uart1, rpu0
```

Renode must be driven via `renode-test`/robot; a bare `renode script.resc`
hangs headless. The R5 core is `rpu0`; reference it after
`using sysbus.cluster1`.

## Current status / remaining work

The Linux side is fully up: `remoteproc` loads the (TCM-linked, trimmed) R5
firmware, `virtio_rpmsg_bus` comes online, and `/dev/rpmsg_ctrl0` +
`/sys/bus/rpmsg/devices/{rpmsg_ctrl,rpmsg_ns}` appear — the RFC's exact
interface. The R5 now **executes** the firmware (prints on `uart0`).

**R5-side bring-up fix chain** (each step found the next fault; all real
ZynqMP-R5-OpenAMP requirements this board did not have out of the box):

1. **TCM link** — firmware must be TCM-resident (above).
2. **`zephyr,ipc = &rpu0_apu_mailbox`** — the `ipm_xlnx_ipi` driver puts the IPM
   API on the *child* mailbox device; the parent `rpu0_ipi` has a NULL api.
   Pointing `zephyr,ipc` at the parent faults in `ipm_set_enabled`.
3. **MPU region for the shared memory** — the ZynqMP SoC uses a *static* MPU
   table (`soc/xlnx/zynqmp/arm_mpu_regions.c`) that does not map the DDR
   OpenAMP carve-out, so `zephyr,memory-attr` is ignored and the R5 data-aborts
   in `virtqueue`/`rpmsg_init_vdev`. See `zynqmp-r5-openamp-mpu.patch` (a
   non-cacheable region for the carve-out; candidate to upstream or move to a
   board-level MPU region).

With all three, the R5 no longer faults and runs the OpenAMP firmware.

**Open blocker:** after `remoteproc start` the emulation nearly stalls (very
little virtual time advances) and no R5-announced rpmsg channel has been
observed yet — consistent with an IPI notify / interrupt-storm issue in the
R5↔A53 handshake. Under investigation.

Then:
1. Complete the R5 rpmsg handshake (resolve the post-start stall; verify a
   channel appears on the Linux side).
2. Swap the echo app for the **Pouch device + rpmsg adapter** firmware (which
   must also fit TCM).
3. Replace the demo's Buildroot rootfs with an **Ubuntu userspace** rootfs.
4. Add a **mock broker** on the Linux side and a Robot test asserting a Pouch
   round-trip over real rpmsg.
