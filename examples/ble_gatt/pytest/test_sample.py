import pytest
import trio
import logging
import subprocess

LOGGER = logging.getLogger(__name__)

pytestmark = pytest.mark.anyio

async def test_gw(gateway, board):
    await board.wait_for_regex_in_line('.*Failed to load certificate', timeout_s=180.0)

    await board.send_cmd('log halt')

    # Set Golioth credential

    subprocess.run(["smpmgr", "--port", board.port, "--mtu", "128", "file", "upload", "/home/mike/Downloads/salmon-itchy-bobcat.crt.der", "/lfs1/credentials/crt.der"])
    subprocess.run(["smpmgr", "--port", board.port, "--mtu", "128", "file", "upload", "/home/mike/Downloads/salmon-itchy-bobcat.key.der", "/lfs1/credentials/key.der"])

    await board.send_cmd('log go')
    await board.send_cmd('kernel reboot')

    assert None is not await board.wait_for_regex_in_line(".*Received LED setting", timeout_s=180.0)

