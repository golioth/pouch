#include <pouch/port.h>

int _other_test_number;

static void set_other_test_number(void)
{
    _other_test_number = 1337;
}
POUCH_APPLICATION_STARTUP_HOOK(set_other_test_number);

int get_other_test_number(void)
{
    return _other_test_number;
}
