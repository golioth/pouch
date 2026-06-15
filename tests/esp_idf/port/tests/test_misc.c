#include "unity.h"
#include <pouch/port.h>
#include <stdint.h>

POUCH_STATIC_ASSERT(1 == 1, "Compile-time sanity check");

void test_div_round_up_exact(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, DIV_ROUND_UP(6, 2), "6 / 2 rounds up to 3");
}

void test_div_round_up_remainder(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, DIV_ROUND_UP(10, 3), "10 / 3 rounds up to 4");
}

void test_div_round_up_by_one(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, DIV_ROUND_UP(7, 1), "7 / 1 rounds up to 7");
}

void test_in_range_inside(void)
{
    TEST_ASSERT_TRUE_MESSAGE(IN_RANGE(5, 0, 10), "5 is in range 0-10");
}

void test_in_range_below(void)
{
    TEST_ASSERT_FALSE_MESSAGE(IN_RANGE(-1, 0, 10), "-1 is not in range 0-10");
}

void test_in_range_above(void)
{
    TEST_ASSERT_FALSE_MESSAGE(IN_RANGE(11, 0, 10), "11 is not in range 0-10");
}

void test_in_range_at_boundaries(void)
{
    TEST_ASSERT_TRUE_MESSAGE(IN_RANGE(0, 0, 10), "0 is in range 0-10");
    TEST_ASSERT_TRUE_MESSAGE(IN_RANGE(10, 0, 10), "10 is in range 0-10");
}

void test_min_a_less(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, MIN(3, 7), "MIN(3, 7) should be 3");
}

void test_min_b_less(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, MIN(5, 2), "MIN(5, 2) should be 2");
}

void test_min_equal(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, MIN(4, 4), "MIN(4, 4) should be 4");
}

void test_log2_powers_of_two(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, LOG2(1), "LOG2(1) should be 0");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, LOG2(2), "LOG2(2) should be 1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, LOG2(4), "LOG2(4) should be 2");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, LOG2(8), "LOG2(8) should be 3");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, LOG2(128), "LOG2(128) should be 7");
    TEST_ASSERT_EQUAL_INT_MESSAGE(31, LOG2(0x80000000), "LOG2(0x80000000) should be 31");
}

void test_log2_non_powers(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, LOG2(3), "LOG2(3) should be 1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, LOG2(7), "LOG2(7) should be 2");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, LOG2(15), "LOG2(15) should be 3");
}

TEST_CASE("Misc", "[pouch][misc]")
{
    RUN_TEST(test_div_round_up_exact);
    RUN_TEST(test_div_round_up_remainder);
    RUN_TEST(test_div_round_up_by_one);
    RUN_TEST(test_in_range_inside);
    RUN_TEST(test_in_range_below);
    RUN_TEST(test_in_range_above);
    RUN_TEST(test_in_range_at_boundaries);
    RUN_TEST(test_min_a_less);
    RUN_TEST(test_min_b_less);
    RUN_TEST(test_min_equal);
    RUN_TEST(test_log2_powers_of_two);
    RUN_TEST(test_log2_non_powers);
}
