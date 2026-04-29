#include "unity.h"
#include "pouch/port.h"
#include <string.h>
#include "errno.h"

#define MSGQ_SIZE 4
#define MSG_SIZE sizeof(int)

static pouch_msgq_t msgq;
static uint8_t msgq_buffer[MSGQ_SIZE * MSG_SIZE];

void initialize_msgq(void)
{
    pouch_msgq_init(&msgq, msgq_buffer, sizeof(msgq_buffer), MSG_SIZE);
}

void test_msgq_send_and_receive_basic(void)
{
    initialize_msgq();

    int val_in = 42;
    int val_out = 0;

    TEST_ASSERT_EQUAL_INT(0, pouch_msgq_put(&msgq, &val_in, 10));
    TEST_ASSERT_EQUAL_INT(0, pouch_msgq_get(&msgq, &val_out, 10));
    TEST_ASSERT_EQUAL_INT(val_in, val_out);
}

void test_msgq_returns_eagain_on_full(void)
{
    initialize_msgq();

    int vals[MSGQ_SIZE + 1];
    for (int i = 0; i < MSGQ_SIZE; ++i)
    {
        vals[i] = i;
        TEST_ASSERT_EQUAL_INT(0, pouch_msgq_put(&msgq, &vals[i], 0));
    }
    int extra = 99;
    TEST_ASSERT_EQUAL_INT(-EAGAIN, pouch_msgq_put(&msgq, &extra, 0));
}

void test_msgq_returns_enomsg_on_empty(void)
{
    initialize_msgq();

    int val_out = 0;
    TEST_ASSERT_EQUAL_INT(-ENOMSG, pouch_msgq_get(&msgq, &val_out, 0));
}

void test_msgq_fifo_order(void)
{
    initialize_msgq();

    int vals_in[MSGQ_SIZE] = {10, 20, 30, 40};
    int val_out = 0;

    for (int i = 0; i < MSGQ_SIZE; ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, pouch_msgq_put(&msgq, &vals_in[i], 10));
    }
    for (int i = 0; i < MSGQ_SIZE; ++i)
    {
        TEST_ASSERT_EQUAL_INT(0, pouch_msgq_get(&msgq, &val_out, 10));
        TEST_ASSERT_EQUAL_INT(vals_in[i], val_out);
    }
}

void test_msgq_can_be_reused_after_emptying(void)
{
    initialize_msgq();

    int val_in = 55, val_out = 0;
    TEST_ASSERT_EQUAL_INT(0, pouch_msgq_put(&msgq, &val_in, 10));
    TEST_ASSERT_EQUAL_INT(0, pouch_msgq_get(&msgq, &val_out, 10));
    TEST_ASSERT_EQUAL_INT(val_in, val_out);

    val_in = 77;
    TEST_ASSERT_EQUAL_INT(0, pouch_msgq_put(&msgq, &val_in, 10));
    TEST_ASSERT_EQUAL_INT(0, pouch_msgq_get(&msgq, &val_out, 10));
    TEST_ASSERT_EQUAL_INT(val_in, val_out);
}

int run_unity_msgq_tests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_msgq_send_and_receive_basic);
    RUN_TEST(test_msgq_returns_eagain_on_full);
    RUN_TEST(test_msgq_returns_enomsg_on_empty);
    RUN_TEST(test_msgq_fifo_order);
    RUN_TEST(test_msgq_can_be_reused_after_emptying);
    return UNITY_END();
}
