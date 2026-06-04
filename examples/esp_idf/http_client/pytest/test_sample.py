#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import datetime
import base64
from dataclasses import dataclass
import re
import time

import anyio
import pytest
from pexpect.exceptions import TIMEOUT as PexpectTimeout

pytestmark = pytest.mark.anyio

CONNECT_TIMEOUT_S = 20.0
STORE_TIMEOUT_S = 20.0
SYNC_TIMEOUT_S = 180.0
LED_TIMEOUT_S = 90.0
REBOOT_TIMEOUT_S = 120.0
BOOT_STATE_TIMEOUT_S = 90.0
STREAM_POLL_ATTEMPTS = 24
STREAM_POLL_DELAY_S = 5.0
BOOT_INITIALIZED_TEXT = "Pouch successfully initialized"
BOOT_STATE_PATTERN = re.compile(
    r".*(Failed to load credentials|credentials_nvs: Failed to|"
    r"Failed to read (wifi_ssid|wifi_psk|crt_der|key_der) len|"
    r"Failed to load .* from NVS|Failed to Load|Returned from app_main|"
    rf"{re.escape(BOOT_INITIALIZED_TEXT)})"
)


@dataclass
class CloudSession:
    dut: object
    project: object
    cloud_device: object
    initial_led: bool


def _match_text(match) -> str:
    text = match.group(0)
    if isinstance(text, bytes):
        return text.decode("utf-8", errors="ignore")
    return str(text)


async def _dut_write(dut, command: str) -> None:
    await anyio.to_thread.run_sync(dut.write, command)


async def _dut_expect(dut, pattern, timeout: float):
    return await anyio.to_thread.run_sync(lambda: dut.expect(pattern, timeout=timeout))


async def _dut_hard_reset(dut) -> None:
    await anyio.to_thread.run_sync(dut.serial.hard_reset)


async def _send_and_expect_store(dut, command: str, expected_log: str) -> None:
    await _dut_write(dut, command)
    match = await _dut_expect(
        dut,
        r".*(Successfully stored .+|Failed to store .+|Failed to set .+|Failed to decode base64.*)",
        timeout=STORE_TIMEOUT_S,
    )
    text = _match_text(match)
    if ("Successfully stored" not in text) or (re.search(expected_log, text) is None):
        command_name = command.split(" ", 1)[0]
        raise AssertionError(
            f"Credential store failed for command: {command_name} <secret>"
        )


async def _detect_boot_state(dut, banner_timeout_s: float) -> bool:
    """Return True when boot logs indicate credentials must be provisioned."""

    await _dut_expect(dut, r".*Pouch HTTP Client Example", timeout=banner_timeout_s)
    state_match = await _dut_expect(
        dut, BOOT_STATE_PATTERN, timeout=BOOT_STATE_TIMEOUT_S
    )
    if BOOT_INITIALIZED_TEXT not in _match_text(state_match):
        print("Missing credentials detected; provisioning immediately")
        return True
    return False


async def _wait_for_boot_state(dut) -> bool:
    """Return True when provisioning is required; reset once if boot state goes undetected"""

    last_exception = None

    for timeout_s, do_reset in (
        (CONNECT_TIMEOUT_S, False),
        (REBOOT_TIMEOUT_S, True),
    ):
        if do_reset:
            print("Boot state not detected, forcing hard reset")
            await _dut_hard_reset(dut)

        try:
            return await _detect_boot_state(dut, timeout_s)
        except (PexpectTimeout, AssertionError) as exc:
            last_exception = exc

    raise AssertionError(
        f"Timed out waiting for boot state. Check DUT log: {dut.logfile}"
    ) from last_exception


async def _provision_and_boot(dut, provisioning_creds) -> None:
    crt_der_b64 = base64.b64encode(provisioning_creds.certs.crt_der).decode("ascii")
    key_der_b64 = base64.b64encode(provisioning_creds.certs.key_der).decode("ascii")

    print("Provisioning WiFi and credentials")
    await _send_and_expect_store(
        dut,
        f"ssid {provisioning_creds.wifi.ssid}",
        r".*Successfully stored WiFi SSID",
    )
    await _send_and_expect_store(
        dut,
        f"psk {provisioning_creds.wifi.psk}",
        r".*Successfully stored WiFi PSK",
    )
    await _send_and_expect_store(
        dut,
        f"crt {crt_der_b64}",
        r".*Successfully stored Device CRT",
    )
    await _send_and_expect_store(
        dut,
        f"key {key_der_b64}",
        r".*Successfully stored Device KEY",
    )

    print("Rebooting after provisioning")
    await _dut_write(dut, "reset")
    await _dut_expect(dut, r".*Pouch HTTP Client Example", timeout=REBOOT_TIMEOUT_S)
    await _dut_expect(
        dut, rf".*{re.escape(BOOT_INITIALIZED_TEXT)}", timeout=REBOOT_TIMEOUT_S
    )


async def _require_cloud_device(project, cloud_config: dict[str, str]):
    return await project.device_by_name(cloud_config["device_name"])


def _iso8601_utc(timestamp: datetime.datetime) -> str:
    return timestamp.strftime("%Y-%m-%dT%H:%M:%S.%fZ")


async def _wait_for_sensor_uplink(device, start_time: datetime.datetime) -> dict:
    latest_payload = None
    latest_payload_with_temp = None

    # Use retries, and don't send an end timestamp. This allows time for pouch sent through a
    # gateway to propagate to the server while also filtering values that arrived before the test
    # began.
    for _ in range(STREAM_POLL_ATTEMPTS):
        response = await device.stream.get(
            start=_iso8601_utc(start_time),
            per_page=20,
        )
        entries = response.get("list", [])

        for entry in entries:
            payload = entry.get("data", {})
            if isinstance(payload, dict):
                latest_payload = payload
                if "temp" in payload:
                    latest_payload_with_temp = payload
                    return payload

        await anyio.sleep(STREAM_POLL_DELAY_S)

    assert latest_payload is not None, "No stream entries found in cloud"
    assert latest_payload_with_temp is not None, (
        f"No stream payload with 'temp' found in recent entries. Last payload: {latest_payload}"
    )
    return latest_payload_with_temp


async def _wait_for_led_setting(dut, timeout_s: float) -> bool:
    index = await _dut_expect(
        dut,
        [
            r".*Received LED setting: 0",
            r".*Received LED setting: 1",
        ],
        timeout=timeout_s,
    )
    return bool(index)


async def _get_device_led_setting(device) -> bool | None:
    device_id = str(getattr(device, "id", "") or "")

    settings = await device.settings.get_all()
    if isinstance(settings, dict):
        settings = settings.get("list", [])

    for setting in settings:
        if setting.get("key") != "LED":
            continue

        setting_device_id = str(setting.get("deviceId", "") or "")
        if not setting_device_id:
            continue

        if device_id and setting_device_id != device_id:
            continue

        value = setting.get("value")
        if value == "true":
            return True
        if value == "false":
            return False

    return None


@pytest.fixture(scope="function")
async def connected_cloud_session(cloud_config, provisioning_creds, dut, project):
    needs_provisioning = await _wait_for_boot_state(dut)

    if needs_provisioning:
        await _provision_and_boot(dut, provisioning_creds)

    cloud_device = await _require_cloud_device(project, cloud_config)
    initial_led = await _wait_for_led_setting(dut, timeout_s=SYNC_TIMEOUT_S)

    yield CloudSession(
        dut=dut,
        project=project,
        cloud_device=cloud_device,
        initial_led=initial_led,
    )


async def test_sensor_uplink_contains_temp(connected_cloud_session):
    cloud_device = connected_cloud_session.cloud_device
    start_time = datetime.datetime.now(datetime.UTC)

    await _wait_for_sensor_uplink(cloud_device, start_time)


async def test_led_setting_downlink(connected_cloud_session):
    dut = connected_cloud_session.dut
    cloud_device = connected_cloud_session.cloud_device
    initial_led = connected_cloud_session.initial_led

    print(f"Initial LED observed on DUT: {int(initial_led)}")
    current_led = await _get_device_led_setting(cloud_device)
    if current_led is None:
        current_led = initial_led
        print(
            "No matching device-level LED override found in cloud; "
            f"falling back to observed value {int(current_led)}"
        )
    else:
        print(f"Current device-level LED override in cloud: {int(current_led)}")

    next_led = not current_led
    write_start = time.monotonic()
    print(f"Setting device-level LED override to: {int(next_led)}")
    await cloud_device.settings.set("LED", next_led)
    expected_pattern = (
        r".*Received LED setting: 1" if next_led else r".*Received LED setting: 0"
    )
    await _dut_expect(dut, expected_pattern, timeout=LED_TIMEOUT_S)
    elapsed = time.monotonic() - write_start
    print(f"Device received LED setting {int(next_led)} after {elapsed:.1f}s")
