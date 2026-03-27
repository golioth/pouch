#include <stdio.h>
#include <pouch/port.h>
#include "other_test_functions.h"

POUCH_LOG_REGISTER(main, POUCH_LOG_LEVEL_DBG);

int _startup_number;

void init_startup_number(void)
{
    _startup_number = 42;
}
POUCH_APPLICATION_STARTUP_HOOK(init_startup_number);

void test_logging(void)
{
    POUCH_LOG_ERR("This is an error: %i", 1);
    POUCH_LOG_WRN("This is a warning: %i", 2);
    POUCH_LOG_INF("This is informational: %i", 3);
    POUCH_LOG_DBG("This is debugging: %i", 4);
    POUCH_LOG_VERBOSE("This is verbose: %i", 5);
}

void app_main(void)
{
    POUCH_LOG_INF("Hello, World!");
    POUCH_LOG_INF("Startup number: %i", _startup_number);
    POUCH_LOG_INF("Other test number: %i", get_other_test_number());

    test_logging();
    test_other_logging();
}
