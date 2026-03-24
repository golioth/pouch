/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @file types.h
 * @brief Type declarations for Pouch Transport layer implementations
 */

/** Return type for Pouch data filling functions */
enum pouch_result
{
    /** No more data is available */
    POUCH_NO_MORE_DATA,
    /** More data is available */
    POUCH_MORE_DATA,
    /** An error occurred */
    POUCH_ERROR,
};
