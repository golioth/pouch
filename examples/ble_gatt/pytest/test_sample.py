import pytest
import trio
import logging
import subprocess

LOGGER = logging.getLogger(__name__)

pytestmark = pytest.mark.anyio

LED_SETTING_KEY = 'LED'

async def test_settings(ble_device, board, project):
    # Get current LED setting
    settings = await ble_device.settings.get_all()
    led_setting_value = None
    for setting in settings:
        if setting['key'] == LED_SETTING_KEY:
            led_setting_value = setting['value']
    assert led_setting_value is not None

    # Toggle LED setting
    new_led_setting_value = not led_setting_value
    await ble_device.settings.set(LED_SETTING_KEY, new_led_setting_value)
    await board.wait_for_regex_in_line(f'.*LED setting: {int(new_led_setting_value)}', timeout_s=60.0)

    await ble_device.settings.set(LED_SETTING_KEY, led_setting_value)
    await board.wait_for_regex_in_line(f'.*LED setting: {int(led_setting_value)}', timeout_s=60.0)
