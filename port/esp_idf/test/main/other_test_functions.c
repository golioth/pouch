#include <pouch/port.h>

int _other_test_number;

POUCH_LOG_REGISTER(other_test_functions, POUCH_LOG_LEVEL_DBG);

static void set_other_test_number(void)
{
    _other_test_number = 1337;
}
POUCH_APPLICATION_STARTUP_HOOK(set_other_test_number);

int get_other_test_number(void)
{
    return _other_test_number;
}

void test_other_logging(void)
{
    POUCH_LOG_ERR("This %s goes on hotdogs", "mustard");
    POUCH_LOG_WRN("This %s is for hamburgers", "ketchup");
    POUCH_LOG_INF("%s is great in tuna salad", "Relish");
    POUCH_LOG_DBG("Sandwiches need %s", "pickles");
    POUCH_LOG_VERBOSE("%s tastes icky", "Marmite");

    uint8_t buf[] = {255, 254, 253, 252, 251, 250};
    POUCH_LOG_HEXDUMP(buf, sizeof(buf), "This is a hex dump of buf");
}
