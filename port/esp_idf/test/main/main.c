#include <stdio.h>
#include <pouch/port.h>
#include "other_test_functions.h"

int _startup_number;

void init_startup_number(void)
{
    _startup_number = 42;
}
POUCH_APPLICATION_STARTUP_HOOK(init_startup_number);

void app_main(void)
{
    printf("Hello, World!\n");
    printf("Startup number: %i\n", _startup_number);
    printf("Other test number: %i\n", get_other_test_number());
}
