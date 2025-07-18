/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * Syncs active Golioth services to the Golioth platform.
 *
 * @return 0 on success or a negative error code on failure.
 */
int golioth_sync_to_cloud(void);
