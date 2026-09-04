#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import queue
import secrets
import subprocess
import threading
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import BinaryIO

import pytest
import serial

logger = logging.getLogger(__name__)


def pytest_addoption(parser):
    parser.addoption(
        "--gateway-port",
        type=str,
        help="Serial port for gateway provisioning",
    )
    parser.addoption(
        "--gateway-log-file",
        type=Path,
        default=Path("gateway.log"),
        help="Path for captured gateway serial output",
    )


@pytest.fixture(scope="module")
def gateway_log_path(request):
    return request.config.getoption("--gateway-log-file").resolve()


@pytest.fixture(scope="module")
def gateway_serial_port(request):
    return request.config.getoption("--gateway-port")


@pytest.fixture(scope="module")
def gateway_device_name():
    return "hil-gw-" + secrets.token_hex(4)


@pytest.fixture(scope="module")
async def gateway_cloud_device(project, gateway_device_name):
    created = False

    devices = await project.get_devices({"deviceName": gateway_device_name})
    if devices:
        gw = devices[0]
    else:
        gw = await project.create_device(gateway_device_name, gateway_device_name)
        created = True

    yield gw

    if created:
        await project.delete_device(gw)


@pytest.fixture(scope="module")
def gateway_creds_dir(creds_dir):
    d = creds_dir / "gateway"
    d.mkdir(mode=0o755, exist_ok=True, parents=True)
    return d


@pytest.fixture(scope="module")
def gateway_creds(creds, creds_dir, gateway_creds_dir, gateway_cloud_device, project):
    """Generate gateway DTLS credentials using the same CA as the peripheral."""

    ca_key = (creds_dir / "ca.key.pem").resolve()
    ca_cert = (creds_dir / "ca.crt.pem").resolve()

    name = gateway_cloud_device.name

    logger.info("Generate gateway device private key and cert (signed by shared CA)")

    subprocess.run(
        f"openssl ecparam -name prime256v1 -genkey -noout -out {name}.key.pem",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"""\
    openssl req -new \
        -key {name}.key.pem \
        -subj "/C=US/O={project.id}/CN={name}" \
        -out {name}.csr.pem""",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"""\
    openssl x509 -req \
        -in "{name}.csr.pem" \
        -CA "{ca_cert}" \
        -CAkey "{ca_key}" \
        -CAcreateserial \
        -out "{name}.crt.pem" \
        -days 500 -sha256""",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )

    logger.info("Convert gateway key and cert to DER format")

    subprocess.run(
        f"openssl x509 -in {name}.crt.pem -outform DER -out crt.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )
    subprocess.run(
        f"openssl ec -in {name}.key.pem -outform DER -out key.der",
        check=True,
        shell=True,
        cwd=gateway_creds_dir,
    )


_GATEWAY_READY_PATTERN = b"Scanning successfully started"
_GATEWAY_READY = object()
_GATEWAY_READY_TIMEOUT_S = 120.0
_GATEWAY_BAUDRATE = 115_200
_GATEWAY_READ_TIMEOUT_S = 0.5
_GATEWAY_READER_JOIN_TIMEOUT_S = 5.0
_GATEWAY_LOG_TAIL_BYTES = 200


def _read_gateway_output(
    gateway_serial: serial.Serial,
    log_file: BinaryIO,
    stop_event: threading.Event,
    status_queue: queue.Queue[object],
):
    overlap = b""
    ready = False

    try:
        while not stop_event.is_set():
            data = gateway_serial.read(4096)
            if not data:
                continue

            log_file.write(data)

            if ready:
                continue

            marker_window = overlap + data
            if _GATEWAY_READY_PATTERN in marker_window:
                ready = True
                status_queue.put(_GATEWAY_READY)
                continue

            overlap = marker_window[-(len(_GATEWAY_READY_PATTERN) - 1) :]
    except Exception as error:
        if stop_event.is_set():
            return
        if ready:
            logger.exception("Gateway serial capture stopped")
        else:
            status_queue.put(error)


@contextmanager
def _capture_gateway_output(
    serial_port: str, log_path: Path
) -> Iterator[queue.Queue[object]]:
    stop_event = threading.Event()
    status_queue: queue.Queue[object] = queue.Queue()

    with (
        log_path.open("wb", buffering=0) as log_file,
        serial.Serial(
            serial_port,
            baudrate=_GATEWAY_BAUDRATE,
            timeout=_GATEWAY_READ_TIMEOUT_S,
        ) as gateway_serial,
    ):
        reader = threading.Thread(
            name="gateway-log-reader",
            target=_read_gateway_output,
            args=(gateway_serial, log_file, stop_event, status_queue),
            daemon=True,
        )
        reader.start()

        try:
            yield status_queue
        finally:
            stop_event.set()
            reader.join(timeout=_GATEWAY_READER_JOIN_TIMEOUT_S)
            if reader.is_alive():
                logger.error(
                    "Gateway serial reader did not stop within %.1fs",
                    _GATEWAY_READER_JOIN_TIMEOUT_S,
                )


def _gateway_log_tail(log_path: Path) -> str:
    with log_path.open("rb") as log_file:
        log_file.seek(0, 2)
        log_file.seek(max(0, log_file.tell() - _GATEWAY_LOG_TAIL_BYTES))
        return log_file.read(_GATEWAY_LOG_TAIL_BYTES).decode(errors="replace")


def _wait_for_gateway_ready(status_queue: queue.Queue[object], log_path: Path) -> None:
    try:
        status = status_queue.get(timeout=_GATEWAY_READY_TIMEOUT_S)
    except queue.Empty:
        pytest.fail(
            f"Gateway failed to start scanning within {_GATEWAY_READY_TIMEOUT_S:g}s. "
            f"Last captured output: {_gateway_log_tail(log_path)}\n"
            f"Full gateway log: {log_path}"
        )

    if status is _GATEWAY_READY:
        return

    cause = status if isinstance(status, Exception) else None
    reason = (
        f"serial capture failed before readiness; output is in {log_path}"
        if cause is not None
        else f"unexpected capture status: {status!r}"
    )
    raise RuntimeError(f"Gateway {reason}") from cause


@pytest.fixture(scope="module", autouse=True)
def provisioned_gateway(request, gateway_serial_port):
    if not gateway_serial_port:
        logger.info("No gateway port configured; skipping gateway provisioning")
        yield
        return

    logger.info("Uploading gateway credentials via smpmgr")

    request.getfixturevalue("gateway_creds")  # Need to generate the gateway credentials
    creds_dir = request.getfixturevalue("gateway_creds_dir")
    log_path = request.getfixturevalue("gateway_log_path")

    for local_name, remote_path in [
        ("crt.der", "/lfs1/credentials/crt.der"),
        ("key.der", "/lfs1/credentials/key.der"),
    ]:
        subprocess.run(
            [
                "smpmgr",
                "--port",
                gateway_serial_port,
                "file",
                "upload",
                str(creds_dir / local_name),
                remote_path,
            ],
            check=True,
        )
        logger.info("Uploaded %s -> %s", local_name, remote_path)

    logger.info("Resetting gateway")
    subprocess.run(
        [
            "smpmgr",
            "--port",
            gateway_serial_port,
            "os",
            "reset",
        ],
        check=True,
    )

    logger.info("Starting gateway serial capture to %s", log_path)

    with _capture_gateway_output(gateway_serial_port, log_path) as status_queue:
        logger.info(
            "Waiting for gateway ready pattern: %s",
            _GATEWAY_READY_PATTERN.decode(),
        )
        _wait_for_gateway_ready(status_queue, log_path)
        logger.info("Gateway provisioned and ready")
        yield
