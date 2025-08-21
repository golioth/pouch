/*
 * Copyright (c) 2025 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/iterable_sections.h>

#define GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN 32

/* Manifests handler registration */

struct golioth_ota_manifest_component
{
    const char *name;
    const char *current;
    const char *target;
    const uint8_t *target_hash;
    size_t size;
};

/** Callback for receiving an OTA manifest.
 *
 * @param components Pointer to an array of components.
 * @param num_components The number of items in \ref components.
 */
typedef void (*golioth_ota_manifest_receive)(
    const struct golioth_ota_manifest_component *components,
    size_t num_components);

struct golioth_ota_manifest_handler
{
    golioth_ota_manifest_receive receive;
};

/**
 * Register a handler for an OTA manifest.
 *
 * This handler will be executed whenever the device receives a new OTA
 * manifest from the Golioth Cloud.
 *
 * Note: Only one handler can be registered.
 *
 * @param _handler The manifest handler to register.
 */
#define GOLIOTH_OTA_MANIFEST_HANDLER(_handler)                                            \
    const STRUCT_SECTION_ITERABLE(golioth_ota_manifest_handler, ota_manifest_handler) = { \
        .receive = _handler,                                                              \
    }

/* Component handler registration */

/**
 * Callback for receiving data for a registered OTA component.
 *
 * @param data Pointer to a block of the component.
 * @param offset Offset of \ref data within the component.
 * @param len Length of \ref data.
 * @param is_last True if this is the last block of the component.
 */
typedef void (*golioth_ota_component_receive)(const void *data,
                                              size_t offset,
                                              size_t len,
                                              bool is_last);

struct golioth_ota_registered_component_data
{
    char target[CONFIG_GOLIOTH_OTA_MAX_VERSION_LEN];
    uint8_t target_hash[GOLIOTH_OTA_COMPONENT_HASH_BIN_LEN];
    size_t size;
    uint8_t state;
};

struct golioth_ota_registered_component
{
    const char *name;
    const char *version;
    golioth_ota_component_receive receive;
    struct golioth_ota_registered_component_data *data;
};

/**
 * Register an OTA component.
 *
 * OTA components must be registered in order to be reflected in manifests and
 * downloaded from the Golioth Cloud.
 *
 * @param _name The name of the C structure holding this information.
 * @param _package The name of the package on Golioth.
 * @param _version The current version of the component.
 * @param _receive A callback for accepting blocks of data for the component.
 */
#define GOLIOTH_OTA_COMPONENT(_name, _package, _version, _receive)              \
    struct golioth_ota_registered_component_data ota_component_data_##_name = { \
        .target = _version,                                                     \
        .state = 0,                                                             \
    };                                                                          \
    const STRUCT_SECTION_ITERABLE(golioth_ota_registered_component,             \
                                  CONCAT(ota_component_, _name)) = {            \
        .name = _package,                                                       \
        .version = _version,                                                    \
        .receive = _receive,                                                    \
        .data = &ota_component_data_##_name,                                    \
    }

/* Component state API */

/** Mark a component for download.
 *
 * Components marked for download will be received in the next Downlink.
 *
 * @param name The name of the component to mark.
 */
int golioth_ota_mark_for_download(const char *name);

/** Mark a component as idle.
 *
 * No action will be taken on idle components.
 *
 * @param name The name of the component to mark.
 */
int golioth_ota_mark_idle(const char *name);

/** Mark a component as updating.
 *
 * If one or more components are marked as updating, the cloud will not send
 * any components during downlink.
 *
 * @param name The name of the component to mark.
 */
int golioth_ota_mark_updating(const char *name);
