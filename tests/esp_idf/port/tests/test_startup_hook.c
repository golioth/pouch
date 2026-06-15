#include "unity.h"
#include <pouch/port.h>

static bool hook_a_called;
static bool hook_b_called;
static int hook_value;

static void set_hook_a(void)
{
    hook_a_called = true;
}

static void set_hook_b(void)
{
    hook_b_called = true;
}

static void set_hook_value_fn(void)
{
    hook_value = 42;
}

POUCH_APPLICATION_STARTUP_HOOK(set_hook_a);
POUCH_APPLICATION_STARTUP_HOOK(set_hook_b);
POUCH_APPLICATION_STARTUP_HOOK(set_hook_value_fn);

void test_startup_hook_single(void)
{
    TEST_ASSERT_TRUE_MESSAGE(hook_a_called, "Startup hook A should have been called");
}

void test_startup_hook_multiple(void)
{
    TEST_ASSERT_TRUE_MESSAGE(hook_a_called, "Startup hook A should have been called");
    TEST_ASSERT_TRUE_MESSAGE(hook_b_called, "Startup hook B should have been called");
}

void test_startup_hook_value(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(42, hook_value, "Startup hook should set value to 42");
}

TEST_CASE("Startup Hook", "[pouch][startup_hook]")
{
#ifdef linux
    TEST_IGNORE_MESSAGE("Skipped: not supported by Linux");
#else
    RUN_TEST(test_startup_hook_single);
    RUN_TEST(test_startup_hook_multiple);
    RUN_TEST(test_startup_hook_value);
#endif
}
