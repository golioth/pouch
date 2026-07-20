#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import secrets
from pathlib import Path

import pytest

pytestmark = pytest.mark.anyio


def pytest_addoption(parser):
    parser.addoption(
        "--fw-update-bin",
        type=str,
        help="Path to the firmware update binary for OTA tests",
    )
    parser.addoption(
        "--fw-update-ver",
        type=str,
        help="Version string of the firmware update binary for OTA tests",
    )
    parser.addoption(
        "--fw-update-pkg-name",
        type=str,
        help="OTA package name for the firmware update artifact (required for OTA tests)",
    )


@pytest.fixture(scope="module")
def test_id() -> str:
    return secrets.token_hex(8)


@pytest.fixture(scope="module")
def artifacts_to_cleanup() -> list:
    return []


@pytest.fixture(scope="module")
def fw_update_bin(request: pytest.FixtureRequest) -> Path:
    bin_path = request.config.getoption("--fw-update-bin")
    if not bin_path:
        pytest.skip("--fw-update-bin not provided")
    return Path(bin_path)


@pytest.fixture(scope="module")
def fw_update_ver(request: pytest.FixtureRequest) -> str:
    version = request.config.getoption("--fw-update-ver")
    if not version:
        pytest.skip("--fw-update-ver not provided")
    return version


@pytest.fixture(scope="module")
def pouch_ota_package(request: pytest.FixtureRequest) -> str:
    package = request.config.getoption("--fw-update-pkg-name")
    if not package:
        pytest.skip("--fw-update-pkg-name not provided")
    return package


@pytest.fixture(scope="module")
async def ota_cohort(project, device, test_id, artifacts_to_cleanup):
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

    for artifact_id in artifacts_to_cleanup:
        try:
            await project.artifacts.delete(artifact_id)
        except Exception:
            logging.warning("Artifact %s could not be deleted", artifact_id)


@pytest.fixture(scope="module")
async def ota_update(
    project,
    device,
    ota_cohort,
    pouch_ota_package,
    fw_update_bin,
    fw_update_ver,
    test_id,
    artifacts_to_cleanup,
) -> str:
    existing_artifacts = await project.artifacts.get_all()
    matching = [
        a
        for a in existing_artifacts
        if a.package == pouch_ota_package and a.version == fw_update_ver
    ]

    if matching:
        artifact = matching[0]
        logging.info(
            "Found existing artifact (id=%s) for package=%s, version=%s — skipping upload",
            artifact.id,
            pouch_ota_package,
            fw_update_ver,
        )
        artifacts_to_cleanup.append(artifact.id)
    else:
        logging.info(
            "Uploading OTA artifact: %s, version=%s, package=%s",
            fw_update_bin,
            fw_update_ver,
            pouch_ota_package,
        )
        artifact = await project.artifacts.upload(
            path=fw_update_bin,
            version=fw_update_ver,
            package=pouch_ota_package,
        )
        artifacts_to_cleanup.append(artifact.id)

    logging.info("Creating deployment on cohort '%s'", ota_cohort.name)
    await ota_cohort.deployments.create(
        f"ota-test-{device.name}-{test_id}",
        [artifact.id],
    )

    yield fw_update_ver
