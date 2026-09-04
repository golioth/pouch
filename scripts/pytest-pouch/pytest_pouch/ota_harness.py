#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

import logging
import secrets
from pathlib import Path

import pytest

logger = logging.getLogger(__name__)

pytestmark = pytest.mark.anyio


@pytest.fixture(scope="module")
def test_id() -> str:
    return secrets.token_hex(8)


@pytest.fixture(scope="module")
def artifacts_to_cleanup() -> list:
    return []


@pytest.fixture(scope="module")
def ota_mode(request: pytest.FixtureRequest) -> str:
    """Selected OTA mode (``dummy``, ``firmware``, or ``disabled``)."""
    return request.config.getoption("--ota-mode")


@pytest.fixture(scope="module")
def fw_update_ver(request: pytest.FixtureRequest, ota_mode: str) -> str:
    if ota_mode == "dummy":
        return "dummy_ver"
    cli = request.config.getoption("--fw-update-ver")
    if cli:
        return cli
    try:
        ver = request.getfixturevalue("fw_update_version")
        if ver is not None:
            return ver
    except pytest.FixtureLookupError:
        pass
    pytest.fail(
        "Firmware update version not provided. Pass --fw-update-ver via "
        "--pytest-args, use --ota-mode=dummy, or load a harness plugin that "
        "defines the fw_update_version fixture."
    )


@pytest.fixture(scope="module")
def pouch_ota_package(request: pytest.FixtureRequest, ota_mode: str) -> str:
    if ota_mode == "dummy":
        return "ci_ota_fw"
    cli = request.config.getoption("--fw-update-pkg-name")
    if cli:
        return cli
    try:
        pkg = request.getfixturevalue("fw_update_package")
        if pkg is not None:
            return pkg
    except pytest.FixtureLookupError:
        pass
    pytest.fail(
        "OTA package name not provided. Pass --fw-update-pkg-name via "
        "--pytest-args, use --ota-mode=dummy, or load a harness plugin that "
        "defines the fw_update_package fixture."
    )


@pytest.fixture(scope="module")
async def ota_cohort(project, device, test_id, artifacts_to_cleanup):
    cohort_name = f"{device.name.lower().replace('-', '_')}_{test_id}"
    logger.info("Creating cohort '%s' for device '%s'", cohort_name, device.name)
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
        logger.warning("Cohort %s could not be deleted", cohort_name)

    for artifact_id in artifacts_to_cleanup:
        try:
            await project.artifacts.delete(artifact_id)
        except Exception:
            logger.warning("Artifact %s could not be deleted", artifact_id)


@pytest.fixture(scope="module")
async def ota_update(
    project,
    device,
    ota_cohort,
    pouch_ota_package,
    test_id,
    request,
    artifacts_to_cleanup,
    tmp_path_factory,
    fw_update_ver,
    ota_mode: str,
) -> str:
    if ota_mode == "dummy":
        import hashlib
        import os

        version = f"2.0.0-{device.name}-{test_id}"
        image_size = 400 * 1024
        image_data = os.urandom(image_size)
        expected_sha256 = hashlib.sha256(image_data).hexdigest()

        tmp_path = tmp_path_factory.mktemp("ota")
        image_path = tmp_path / "firmware.bin"
        image_path.write_bytes(image_data)

        logger.info(
            "Uploading OTA artifact (dummy): %d bytes, SHA256=%s",
            image_size,
            expected_sha256,
        )

        artifact = await project.artifacts.upload(
            path=image_path,
            version=version,
            package=pouch_ota_package,
        )
        artifacts_to_cleanup.append(artifact.id)

        logger.info("Creating deployment on cohort '%s'", ota_cohort.name)
        await ota_cohort.deployments.create(
            f"ota-test-{device.name}-{test_id}",
            [artifact.id],
        )

        yield expected_sha256
        return

    fw_bin_cli = request.config.getoption("--fw-update-bin")
    if fw_bin_cli:
        fw_bin = Path(fw_bin_cli)
    else:
        try:
            fw_bin = request.getfixturevalue("fw_update_bin")
        except pytest.FixtureLookupError:
            fw_bin = None
        if fw_bin is None:
            pytest.fail(
                "Firmware update binary not provided. Pass --fw-update-bin via "
                "--pytest-args, use --ota-mode=dummy, or load a harness plugin "
                "that defines the fw_update_bin fixture."
            )

    existing_artifacts = await project.artifacts.get_all()
    matching = [
        a
        for a in existing_artifacts
        if a.package == pouch_ota_package and a.version == fw_update_ver
    ]

    if matching:
        artifact = matching[0]
        logger.info(
            "Found existing artifact (id=%s) for package=%s, version=%s — skipping upload",
            artifact.id,
            pouch_ota_package,
            fw_update_ver,
        )
        artifacts_to_cleanup.append(artifact.id)
    else:
        logger.info(
            "Uploading OTA artifact: %s, version=%s, package=%s",
            fw_bin,
            fw_update_ver,
            pouch_ota_package,
        )
        artifact = await project.artifacts.upload(
            path=fw_bin,
            version=fw_update_ver,
            package=pouch_ota_package,
        )
        artifacts_to_cleanup.append(artifact.id)

    logger.info("Creating deployment on cohort '%s'", ota_cohort.name)
    await ota_cohort.deployments.create(
        f"ota-test-{device.name}-{test_id}",
        [artifact.id],
    )

    yield fw_update_ver
