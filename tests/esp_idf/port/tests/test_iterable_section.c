#include "unity.h"
#include <pouch/port.h>

struct test_iterable_item
{
    int id;
    const char *name;
    int value;
};

struct test_iterable_empty_struct
{
    int dummy;
};

POUCH_STRUCT_SECTION_ITERABLE(test_iterable_item, item_alpha) = {1, "alpha", 100};
POUCH_STRUCT_SECTION_ITERABLE(test_iterable_item, item_beta) = {2, "beta", 200};
POUCH_STRUCT_SECTION_ITERABLE(test_iterable_item, item_gamma) = {3, "gamma", 300};

void test_iterable_foreach(void)
{
    int count = 0;
    POUCH_STRUCT_SECTION_FOREACH(test_iterable_item, it)
    {
        count++;
        if (count == 1)
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(1,
                                          it->id,
                                          "First item id should be 1 (alpha sorts first)");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("alpha", it->name, "First item name should be alpha");
            TEST_ASSERT_EQUAL_INT_MESSAGE(100, it->value, "First item value should be 100");
        }
        else if (count == 2)
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(2,
                                          it->id,
                                          "Second item id should be 2 (beta sorts second)");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("beta", it->name, "Second item name should be beta");
            TEST_ASSERT_EQUAL_INT_MESSAGE(200, it->value, "Second item value should be 200");
        }
        else if (count == 3)
        {
            TEST_ASSERT_EQUAL_INT_MESSAGE(3,
                                          it->id,
                                          "Third item id should be 3 (gamma sorts third)");
            TEST_ASSERT_EQUAL_STRING_MESSAGE("gamma", it->name, "Third item name should be gamma");
            TEST_ASSERT_EQUAL_INT_MESSAGE(300, it->value, "Third item value should be 300");
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, count, "Should have iterated over exactly 3 items");
}

void test_iterable_empty_count(void)
{
    int count = -1;
    POUCH_STRUCT_SECTION_COUNT(test_iterable_empty_struct, &count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count, "Empty struct type should have count 0");
}

void test_iterable_get(void)
{
    const struct test_iterable_item *it = NULL;

    POUCH_STRUCT_SECTION_GET(test_iterable_item, 0, &it);
    TEST_ASSERT_NOT_NULL_MESSAGE(it, "GET index 0 should return a valid pointer");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("alpha", it->name, "GET index 0 should be alpha");

    POUCH_STRUCT_SECTION_GET(test_iterable_item, 1, &it);
    TEST_ASSERT_TRUE_MESSAGE(it != NULL, "GET index 1 should return a valid pointer");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("beta", it->name, "GET index 1 should be beta");

    POUCH_STRUCT_SECTION_GET(test_iterable_item, 2, &it);
    TEST_ASSERT_TRUE_MESSAGE(it != NULL, "GET index 2 should return a valid pointer");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("gamma", it->name, "GET index 2 should be gamma");
}

void test_iterable_multiple_types(void)
{
    int count_a = -1;
    int count_b = -1;

    POUCH_STRUCT_SECTION_COUNT(test_iterable_item, &count_a);
    POUCH_STRUCT_SECTION_COUNT(test_iterable_empty_struct, &count_b);

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, count_a, "test_iterable_item should have 3 items");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, count_b, "test_iterable_empty_struct should have 0 items");
}

void test_iterable_complex_fields(void)
{
    const char *expected_names[] = {"alpha", "beta", "gamma"};
    int expected_values[] = {100, 200, 300};
    int idx = 0;

    POUCH_STRUCT_SECTION_FOREACH(test_iterable_item, it)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(it->name, "Item name should not be NULL");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_names[idx],
                                         it->name,
                                         "Item name should match expected");
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected_values[idx],
                                      it->value,
                                      "Item value should match expected");
        idx++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, idx, "Should have iterated over exactly 3 items");
}

TEST_CASE("Iterable Section", "[pouch][iterable_sections]")
{
#ifdef linux
    TEST_IGNORE_MESSAGE("Skipped: not supported by Linux");
#else
    RUN_TEST(test_iterable_foreach);
    RUN_TEST(test_iterable_empty_count);
    RUN_TEST(test_iterable_get);
    RUN_TEST(test_iterable_multiple_types);
    RUN_TEST(test_iterable_complex_fields);
#endif
}
