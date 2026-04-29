#include "unity.h"
#include "pouch/port.h"

void test_timepoint(void)
{
    pouch_timepoint_t timepoint;

    timepoint = pouch_timepoint_get(POUCH_FOREVER);
    TEST_ASSERT_EQUAL(pouch_timepoint_timeout(timepoint), POUCH_FOREVER);

    timepoint = pouch_timepoint_get(POUCH_NO_WAIT);
    TEST_ASSERT_EQUAL(pouch_timepoint_timeout(timepoint), POUCH_NO_WAIT);

    timepoint = pouch_timepoint_get(1000);
    TEST_ASSERT_TRUE(pouch_timepoint_timeout(timepoint) > 0);
}

TEST_CASE("Time", "[pouch][time]")
{
    RUN_TEST(test_timepoint);
}
