#
# Copyright (c) 2025 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Extract git describe string
execute_process(
  COMMAND git describe --tags --always --dirty
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE GIT_RESULT
  ERROR_QUIET
)

# Fallback if git command fails
if(NOT GIT_RESULT EQUAL 0)
  set(GIT_DESCRIBE "unknown")
endif()

# Validate OUTPUT_FILE variable is set
if(NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "OUTPUT_FILE must be defined")
endif()

# Prepare header content
set(HEADER_TEXT
"/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef SM_VERSION_H
#define SM_VERSION_H

#define SM_VERSION \"${GIT_DESCRIBE}\"

#endif /* SM_VERSION_H */

")

# Write to output file
file(WRITE "${OUTPUT_FILE}" "${HEADER_TEXT}")
