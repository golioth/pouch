/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <esp_console.h>
#include <esp_log.h>
#include <mbedtls/base64.h>
#include "credentials_nvs.h"
#include "esp_err.h"

#define TAG "Console"

enum cred_type
{
    CRED_TYPE_WIFI_SSID,
    CRED_TYPE_WIFI_PSK,
    CRED_TYPE_DEVICE_CRT,
    CRED_TYPE_DEVICE_KEY,
    CRED_TYPE_COUNT,
};

#define USAGE_DEVICE_CRT "crt <your_device_crt_der_in_base64>"
#define USAGE_DEVICE_KEY "key <your_device_key_der_in_base64>"
#define USAGE_WIFI_SSID "ssid <your_wifi_ssid>"
#define USAGE_WIFI_PSK "psk <your_wifi_psk>"

struct cred_context
{
    char *hint;
    cred_nvs_set_fn_t set_fn;
};

static const struct cred_context cred_ctx[CRED_TYPE_COUNT] = {
    [CRED_TYPE_WIFI_SSID] = {.hint = USAGE_WIFI_SSID, .set_fn = cred_set_wifi_ssid},
    [CRED_TYPE_WIFI_PSK] = {.hint = USAGE_WIFI_PSK, .set_fn = cred_set_wifi_psk},
    [CRED_TYPE_DEVICE_CRT] = {.hint = USAGE_DEVICE_CRT, .set_fn = cred_set_device_crt},
    [CRED_TYPE_DEVICE_KEY] = {.hint = USAGE_DEVICE_KEY, .set_fn = cred_set_device_key},
};

static int set_credential(enum cred_type type, int argc, char **argv, char *log_msg)
{
    int err;

    if (2 > argc)
    {
        ESP_LOGE(TAG, "Expected argument: %s", cred_ctx[type].hint);
        err = ESP_ERR_INVALID_ARG;
    }
    else
    {
        err = cred_ctx[type].set_fn(argv[1]);
    }

    if (0 != err)
    {
        ESP_LOGE(TAG, "Failed to store %s: %i", log_msg, err);
    }
    ESP_LOGI(TAG, "Successfully stored %s", log_msg);

    return err;
}

static int set_crt(int argc, char **argv)
{
    return set_credential(CRED_TYPE_DEVICE_CRT, argc, argv, "Device CRT");
}

static int set_key(int argc, char **argv)
{
    return set_credential(CRED_TYPE_DEVICE_KEY, argc, argv, "Device KEY");
}

static int set_ssid(int argc, char **argv)
{
    return set_credential(CRED_TYPE_WIFI_SSID, argc, argv, "WiFi SSID");
}

static int set_psk(int argc, char **argv)
{
    return set_credential(CRED_TYPE_WIFI_PSK, argc, argv, "WiFi PSK");
}

static int reset_device(int argc, char **argv)
{
    esp_restart();
    return 0;
}

static const esp_console_cmd_t cmd_crt = {
    .command = "crt",
    .help = "Store a device CRT",
    .hint = cred_ctx[CRED_TYPE_DEVICE_CRT].hint,
    .func = set_crt,
};

static const esp_console_cmd_t cmd_key = {
    .command = "key",
    .help = "Store a device KEY",
    .hint = cred_ctx[CRED_TYPE_DEVICE_KEY].hint,
    .func = set_key,
};

static const esp_console_cmd_t cmd_ssid = {
    .command = "ssid",
    .help = "Store WiFi SSID",
    .hint = cred_ctx[CRED_TYPE_WIFI_SSID].hint,
    .func = set_ssid,
};

static const esp_console_cmd_t cmd_psk = {
    .command = "psk",
    .help = "Store WiFi PSK",
    .hint = cred_ctx[CRED_TYPE_WIFI_PSK].hint,
    .func = set_psk,
};

static const esp_console_cmd_t cmd_reset = {
    .command = "reset",
    .help = "Reset device",
    .hint = NULL,
    .func = reset_device,
};

void console_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.max_cmdline_length = 1024;
    repl_config.prompt = "pouch>";

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_crt));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_key));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_ssid));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_psk));
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_reset));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    /* Ensure console prompt printing clears before more logs */
    vTaskDelay(pdMS_TO_TICKS(300));
    printf("\n\n");
}
