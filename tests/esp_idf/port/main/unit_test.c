#include <pouch/port.h>
#include "unity.h"
#include "unity_test_runner.h"

POUCH_LOG_REGISTER(unit_test, POUCH_LOG_LEVEL_DBG);

void app_main(void)
{
    POUCH_LOG_INF("Pouch ESP-IDF Port Unit Tests");

    UNITY_BEGIN();
    unity_run_all_tests();
    int failures = UNITY_END();

#ifdef linux
    exit(failures);
#endif
}
