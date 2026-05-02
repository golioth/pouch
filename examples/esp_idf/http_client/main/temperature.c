/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <driver/temperature_sensor.h>

static bool initialized = false;
static temperature_sensor_handle_t temp_handle = NULL;
static temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);

float read_temperature(void)
{
    if (false == initialized)
    {
        ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
        initialized = true;
    }

    float tsens_out = 0;

    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &tsens_out));
    ESP_ERROR_CHECK(temperature_sensor_disable(temp_handle));

    return tsens_out;
}
