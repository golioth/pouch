#include <zephyr/ztest.h>
#include <limits.h>
#include <pouch/port.h>

ZTEST(atomic, test_atomic_inc_dec)
{
    pouch_atomic_t val = POUCH_ATOMIC_INIT(0);
    zassert_equal(pouch_atomic_get_value(&val), 0, "Initial value not zero");

    zassert_equal(pouch_atomic_inc(&val), 0, "Inc should return old value");
    zassert_equal(pouch_atomic_get_value(&val), 1, "Value should be 1 after inc");

    zassert_equal(pouch_atomic_dec(&val), 1, "Dec should return old value");
    zassert_equal(pouch_atomic_get_value(&val), 0, "Value should be 0 after dec");
}

ZTEST(atomic, test_atomic_set_clear)
{
    pouch_atomic_t val = POUCH_ATOMIC_INIT(0);
    zassert_equal(pouch_atomic_set(&val, 42), 0, "Set should return old value");
    zassert_equal(pouch_atomic_get_value(&val), 42, "Value should be 42 after set");

    zassert_equal(pouch_atomic_clear(&val), 42, "Clear should return old value");
    zassert_equal(pouch_atomic_get_value(&val), 0, "Value should be 0 after clear");
}

ZTEST(atomic, test_atomic_overflow_underflow)
{
    pouch_atomic_t val = POUCH_ATOMIC_INIT(0);
    zassert_equal(pouch_atomic_dec(&val), 0, "Dec should return old value");
    zassert_equal(pouch_atomic_get_value(&val), -1, "Value should be -1 after underflow");

    zassert_equal(pouch_atomic_set(&val, LONG_MAX), -1, "Set should return old value");
    zassert_equal(pouch_atomic_inc(&val), LONG_MAX, "Inc should return old value");
    zassert_equal(pouch_atomic_get_value(&val),
                  LONG_MIN,
                  "Value should be LONG_MIN after overflow");
    zassert_equal(pouch_atomic_dec(&val), LONG_MIN, "Dec should return old value");
    zassert_equal(pouch_atomic_get_value(&val),
                  LONG_MAX,
                  "Value should be LONG_MIN after underflow");
}

ZTEST(atomic, test_atomic_bit_ops)
{
    pouch_atomic_t val = POUCH_ATOMIC_INIT(0);

    pouch_atomic_set_bit(&val, 3);
    zassert_true(pouch_atomic_test_bit(&val, 3), "Bit 3 should be set");

    zassert_false(pouch_atomic_test_and_clear_bit(&val, 3) == false,
                  "Bit 3 should be cleared and was set");
    zassert_false(pouch_atomic_test_bit(&val, 3), "Bit 3 should be cleared");

    zassert_false(pouch_atomic_test_and_set_bit(&val, 2), "Bit 2 should not be set before");
    zassert_true(pouch_atomic_test_bit(&val, 2), "Bit 2 should be set now");
}

ZTEST(atomic, test_atomic_bit_array_spanning)
{
    // Define enough bits to span at least 3 underlying pouch_atomic_t elements
    const int total_bits = POUCH_ATOMIC_BITS * 3;
    POUCH_ATOMIC_DEFINE(large_mask, total_bits);

    // Clear all bits
    for (int i = 0; i < total_bits; i++)
        pouch_atomic_clear_bit(large_mask, i);

    // Set a bit at the boundary of the second element
    int target_bit = POUCH_ATOMIC_BITS + 5;
    pouch_atomic_set_bit(large_mask, target_bit);

    zassert_true(pouch_atomic_test_bit(large_mask, target_bit));
    zassert_false(pouch_atomic_test_bit(large_mask, target_bit - 1));
    zassert_false(pouch_atomic_test_bit(large_mask, target_bit + 1));
}

ZTEST_SUITE(atomic, NULL, NULL, NULL, NULL, NULL);
