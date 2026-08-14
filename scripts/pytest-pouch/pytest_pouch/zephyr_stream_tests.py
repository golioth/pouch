#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import datetime

import anyio
import pytest

pytestmark = pytest.mark.anyio

_STREAM_POLL_ATTEMPTS = 24
_STREAM_POLL_DELAY_S = 5.0


def _iso8601_utc(timestamp: datetime.datetime) -> str:
    return timestamp.strftime("%Y-%m-%dT%H:%M:%S.%fZ")


async def test_sensor_stream_uplink(device):
    start_time = datetime.datetime.now(datetime.UTC)

    latest_payload = None
    latest_payload_with_temp = None

    for _ in range(_STREAM_POLL_ATTEMPTS):
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
                    return

        await anyio.sleep(_STREAM_POLL_DELAY_S)

    assert latest_payload is not None, "No stream entries found in cloud"
    assert latest_payload_with_temp is not None, (
        f"No stream payload with 'temp' found in recent entries. "
        f"Last payload: {latest_payload}"
    )
