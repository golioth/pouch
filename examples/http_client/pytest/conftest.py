#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import sys
from pathlib import Path

sys.path.insert(
    0, str(Path(__file__).resolve().parents[3] / "scripts" / "pytest-pouch")
)

pytest_plugins = ["pytest_pouch.plugin"]

import pytest  # noqa: E402


@pytest.fixture(scope="module", autouse=True)
async def setup(project, device, creds):
    logging.info("Delete existing device-level LED setting")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])

    logging.info("Ensure the project-level LED setting exists")
    await project.settings.set("LED", False)

    yield

    logging.info("Delete any existing device-level LED settings (cleanup)")

    settings = await device.settings.get_all()
    for setting in settings:
        if "deviceId" in setting and setting["key"] == "LED":
            await device.settings.delete(setting["key"])


@pytest.fixture(scope="module")
async def ota_firmware(project, tmp_path_factory):
    """Generate a random firmware image, upload as artifact, create release."""
    import hashlib
    import os

    # Clean up any existing releases and artifacts from previous failed runs
    logging.info("Cleaning up any existing OTA releases and artifacts")
    releases = await project.releases.get_all()
    for rel in releases:
        logging.info("Deleting existing release: %s", rel.id)
        await project.releases.delete(rel.id)

    artifacts = await project.artifacts.get_all()
    for artifact in artifacts:
        if artifact.package == "main" and artifact.version == "2.0.0":
            logging.info("Deleting existing artifact: %s", artifact.id)
            await project.artifacts.delete(artifact.id)

    image_size = 400 * 1024
    image_data = os.urandom(image_size)
    expected_sha256 = hashlib.sha256(image_data).hexdigest()

    tmp_path = tmp_path_factory.mktemp("ota")
    image_path = tmp_path / "firmware.bin"
    image_path.write_bytes(image_data)

    logging.info(
        "Uploading OTA artifact: %d bytes, SHA256=%s", image_size, expected_sha256
    )

    artifact = await project.artifacts.upload(
        path=image_path,
        version="2.0.0",
        package="main",
    )

    logging.info("Creating release with rollout enabled")

    release = await project.releases.create(
        artifact_ids=[artifact.id],
        rollout=True,
    )

    yield expected_sha256

    logging.info("Cleaning up OTA release and artifact")

    await project.releases.delete(release.id)
    await project.artifacts.delete(artifact.id)
