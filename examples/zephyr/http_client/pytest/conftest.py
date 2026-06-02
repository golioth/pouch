#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import secrets
import sys
from pathlib import Path

sys.path.insert(
    0, str(Path(__file__).resolve().parents[4] / "scripts" / "pytest-pouch")
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
def test_id():
    """Unique random slug for this test module's cloud resources.

    Generated freshly per pytest module so that parallel CI pipelines,
    sequential retries, and concurrent local runs never collide on
    shared Golioth resources (cohorts, deployments, OTA artifacts).
    """
    return secrets.token_hex(8)


@pytest.fixture(scope="module")
def artifacts_to_cleanup():
    return []


@pytest.fixture(scope="module")
async def cohort(project, device, test_id, artifacts_to_cleanup):
    """Create a per-device, per-test cohort so OTA deployments are isolated."""
    cohort_name = f"{device.name.lower().replace('-', '_')}_{test_id}"
    logging.info("Creating cohort '%s' for device '%s'", cohort_name, device.name)
    cohort = await project.cohorts.create(cohort_name)
    await device.update_cohort(cohort.id)

    yield cohort

    try:
        await device.remove_cohort()
    except Exception:
        pass

    try:
        await project.cohorts.delete(cohort.id)
    except Exception:
        logging.warning("Cohort %s could not be deleted", cohort_name)
        pass

    for artifact_id in artifacts_to_cleanup:
        try:
            await project.artifacts.delete(artifact_id)
        except Exception:
            logging.warning("Artifact %s could not be deleted", artifact_id)


@pytest.fixture(scope="module")
async def ota_firmware(
    project, device, cohort, test_id, tmp_path_factory, artifacts_to_cleanup
):
    """Generate a random firmware image, upload as artifact, deploy to cohort."""
    import hashlib
    import os

    # Version includes test_id (a random slug) so parallel pipelines,
    # retries, and concurrent local runs never collide on this artifact.
    version = f"2.0.0-{device.name}-{test_id}"

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

    artifacts_to_cleanup.append(artifact.id)

    logging.info("Creating deployment on cohort '%s'", cohort.name)

    await cohort.deployments.create(f"ota-test-{device.name}-{test_id}", [artifact.id])

    yield expected_sha256
