#
# Copyright (c) 2026 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

# The order of these imports determines the order in which the tests run, so keep
# it as is.
# isort: skip_file

from pytest_pouch.zephyr_settings_tests import *
from pytest_pouch.zephyr_stream_tests import *
from pytest_pouch.zephyr_ota_tests import *
