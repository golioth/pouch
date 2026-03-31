#include <stdio.h>
#include <pouch/port.h>
#include <pouch/golioth/settings_types.h>
#include "other_test_functions.h"
#include <errno.h>

POUCH_LOG_REGISTER(main, POUCH_LOG_LEVEL_DBG);

/*--------------------------------------------------
 * Iterable Sections
 *------------------------------------------------*/

#include <pouch/golioth/settings_callbacks.h>
#include <string.h>
#include "../../../../golioth_sdk/settings.h"

static int led_setting_cb(bool new_value)
{
    POUCH_LOG_INF("Received LED setting: %s", new_value ? "ON" : "OFF");

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

static int log_level_setting_cb(int32_t new_value)
{
    POUCH_LOG_INF("Received Log Level setting: %" PRIi32, new_value);

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LOG_LEVEL, log_level_setting_cb);

/* Reimplement golioth_settings_receive_one() for testing purposes */
int test_settings_callbacks(const struct setting_value *value)
{
    POUCH_STRUCT_SECTION_FOREACH(golioth_settings_handler, setting)
    {
        if (0 == strcmp(setting->key, value->key))
        {
            if (setting->type != value->type)
            {
                return -EINVAL;
            }

            switch (setting->type)
            {
                case GOLIOTH_SETTING_VALUE_TYPE_INT:
                    return setting->int_cb(value->int_val);

                case GOLIOTH_SETTING_VALUE_TYPE_BOOL:
                    return setting->bool_cb(value->bool_val);

                case GOLIOTH_SETTING_VALUE_TYPE_FLOAT:
                    return setting->float_cb(value->float_val);

                case GOLIOTH_SETTING_VALUE_TYPE_STRING:
                    return setting->string_cb(value->str_val.data, value->str_val.len);

                default:
                    POUCH_LOG_ERR("Unknown settings type");
                    break;
            }
        }
    }

    return 0;
}

void test_iterable_sections(void)
{
    struct setting_value value;
    const char led_key_buf[] = "LED";
    const char log_level_key_buf[] = "LOG_LEVEL";

    value.key = led_key_buf;
    value.type = GOLIOTH_SETTING_VALUE_TYPE_BOOL;
    value.bool_val = true;

    POUCH_LOG_WRN("Test LED callback (expect ON)");
    test_settings_callbacks(&value);

    value.key = log_level_key_buf;
    value.type = GOLIOTH_SETTING_VALUE_TYPE_INT;
    value.int_val = 3;

    POUCH_LOG_WRN("Test LOG_LEVEL callback (expect 3)");
    test_settings_callbacks(&value);

    POUCH_LOG_WRN("Test count sections (expect 2)");
    int sec_count;
    POUCH_STRUCT_SECTION_COUNT(golioth_settings_handler, &sec_count);
    POUCH_LOG_INF("Settings handler count: %i", sec_count);

    POUCH_LOG_WRN("Test section get (expect LOG_LEVEL 1)");
    struct golioth_settings_handler *log_level_handler;
    POUCH_STRUCT_SECTION_GET(golioth_settings_handler, 1, &log_level_handler);
    log_level_handler->int_cb(1);
}

/*--------------------------------------------------
 * Application Startup Hook
 *------------------------------------------------*/

int _startup_number;

void init_startup_number(void)
{
    _startup_number = 42;
}
POUCH_APPLICATION_STARTUP_HOOK(init_startup_number);

/*--------------------------------------------------
 * Big Endian
 *------------------------------------------------*/

void test_big_endian(void)
{
    POUCH_LOG_WRN("Test big endian");


    uint16_t test16 = 0xCDAB;
    uint32_t test32 = 0x01EFCDAB;
    uint64_t test64 = 0x8967452301EFCDAB;

    POUCH_LOG_INF("get be16: 0x%X", pouch_get_be16((uint8_t *) &test16));
    POUCH_LOG_INF("get be32: 0x%" PRIX32, pouch_get_be32((uint8_t *) &test32));
    POUCH_LOG_INF("get be64: 0x%" PRIX64, pouch_get_be64((uint8_t *) &test64));

    uint16_t dst;
    pouch_put_be16(0x3412, (uint8_t *) &dst);
    POUCH_LOG_INF("put be16: 0x%X", dst);
}

/*--------------------------------------------------
 * Logging
 *------------------------------------------------*/

void test_logging(void)
{
    POUCH_LOG_WRN("Test logging");
    POUCH_LOG_ERR("This is an error: %i", 1);
    POUCH_LOG_WRN("This is a warning: %i", 2);
    POUCH_LOG_INF("This is informational: %i", 3);
    POUCH_LOG_DBG("This is debugging: %i", 4);
    POUCH_LOG_VERBOSE("This is verbose: %i", 5);
}

/*--------------------------------------------------
 * Main
 *------------------------------------------------*/

void app_main(void)
{
    POUCH_LOG_INF("Hello, World!");
    POUCH_LOG_INF("Startup number: %i", _startup_number);
    POUCH_LOG_INF("Other test number: %i", get_other_test_number());

    test_logging();
    test_other_logging();
    test_iterable_sections();
    test_big_endian();
}
