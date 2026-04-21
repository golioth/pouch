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
async def cohort(project, device):
    """Create a per-device cohort so OTA deployments target only this device."""
    cohort_name = device.name.lower().replace("-", "_")
    logging.info("Creating cohort '%s' for device '%s'", cohort_name, device.name)
    cohort = await project.cohorts.create(cohort_name)
    await device.update_cohort(cohort.id)

    yield cohort

    try:
        await device.remove_cohort()
    except Exception:
        pass
    await project.cohorts.delete(cohort.id)


@pytest.fixture(scope="module")
async def ota_firmware(project, device, cohort, tmp_path_factory):
    """Generate a random firmware image, upload as artifact, deploy to cohort."""
    import hashlib
    import os

    # Use a device-specific version to avoid collisions with parallel runs
    version = f"2.0.0-{device.name}"

    # Clean up stale artifact from a previous failed run
    logging.info("Cleaning up any existing artifact with version '%s'", version)
    artifacts = await project.artifacts.get_all()
    for artifact in artifacts:
        if artifact.package == "main" and artifact.version == version:
            logging.info("Deleting existing artifact: %s", artifact.id)
            try:
                await project.artifacts.delete(artifact.id)
            except Exception:
                logging.warning(
                    "Could not delete stale artifact %s (may still be "
                    "referenced by a deployment)",
                    artifact.id,
                )

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
        version=version,
        package="main",
    )

    logging.info("Creating deployment on cohort '%s'", cohort.name)

    await cohort.deployments.create(f"ota-test-{device.name}", [artifact.id])

    yield expected_sha256

    logging.info("Cleaning up OTA artifact")

    try:
        await project.artifacts.delete(artifact.id)
    except Exception:
        logging.info(
            "Artifact deletion deferred (still referenced by deployment); "
            "will be cleaned up on next run"
        )
