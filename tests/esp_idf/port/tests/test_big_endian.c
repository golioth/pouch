#include "unity.h"
#include <pouch/port.h>
#include <stdint.h>

void test_get_be16(void)
{
    uint8_t input[] = {0x12, 0x34};
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x1234,
                                     pouch_get_be16(input),
                                     "BE16 0x12 0x34 should decode to 0x1234");
}

void test_get_be16_max(void)
{
    uint8_t input[] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF,
                                     pouch_get_be16(input),
                                     "BE16 0xFF 0xFF should decode to 0xFFFF");
}

void test_get_be16_zero(void)
{
    uint8_t input[] = {0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x0000,
                                     pouch_get_be16(input),
                                     "BE16 0x00 0x00 should decode to 0x0000");
}

void test_get_be32(void)
{
    uint8_t input[] = {0x12, 0x34, 0x56, 0x78};
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x12345678,
                                     pouch_get_be32(input),
                                     "BE32 should decode to 0x12345678");
}

void test_get_be64(void)
{
    uint8_t input[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    uint64_t result = pouch_get_be64(input);

    /* We can't use unity's 64-bit assertions when running on 32-bit systems. We build ESP-IDF unit
     * tests for Linux using 32-bit mode due to FreeRTOS limitations. Workaround the issue by
     * checking the output with two 32-bit asserts. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x12345678,
                                     (uint32_t) (result >> 32),
                                     "BE64 upper 32 bits should be 0x12345678");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x9ABCDEF0,
                                     (uint32_t) result,
                                     "BE64 lower 32 bits should be 0x9ABCDEF0");
}

void test_put_be16(void)
{
    uint8_t dst[2];
    pouch_put_be16(0xABCD, dst);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xAB, dst[0], "BE16 dst[0] should be 0xAB");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xCD, dst[1], "BE16 dst[1] should be 0xCD");
}

void test_be16_roundtrip(void)
{
    uint8_t buf[2];
    pouch_put_be16(0xBEEF, buf);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xBEEF,
                                     pouch_get_be16(buf),
                                     "BE16 roundtrip should return 0xBEEF");
}

TEST_CASE("Big Endian", "[pouch][big_endian]")
{
    RUN_TEST(test_get_be16);
    RUN_TEST(test_get_be16_max);
    RUN_TEST(test_get_be16_zero);
    RUN_TEST(test_get_be32);
    RUN_TEST(test_get_be64);
    RUN_TEST(test_put_be16);
    RUN_TEST(test_be16_roundtrip);
}
