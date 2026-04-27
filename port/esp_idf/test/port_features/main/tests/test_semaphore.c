#include <stdio.h>
#include <stdbool.h>
#include <unity.h>
#include <pouch/port.h>

#define SEM_INIT_COUNT 1
#define SEM_LIMIT 2

POUCH_SEM_DEFINE(static_sem, 0, 1);

void test_sem_init_give_take(void)
{
    pouch_sem_t sem;
    int ret;

    // Initialize semaphore
    ret = pouch_sem_init(&sem, SEM_INIT_COUNT, SEM_LIMIT);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Take should succeed (count goes from 1 to 0)
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Take again should fail (count is 0)
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    TEST_ASSERT_NOT_EQUAL(0, ret);

    // Give should increment count
    pouch_sem_give(&sem);

    // Take should succeed again
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_sem_reset(void)
{
    pouch_sem_t sem;
    int ret;

    ret = pouch_sem_init(&sem, 0, SEM_LIMIT);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Give to increment count
    pouch_sem_give(&sem);

    // Reset should set count to 0
    pouch_sem_reset(&sem);

    // Take should fail (count is 0)
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    TEST_ASSERT_NOT_EQUAL(0, ret);
}

void test_sem_static_define(void)
{
    int ret;

    // Give to increment count from 0 to 1
    pouch_sem_give(&static_sem);

    // Take should succeed (count goes from 1 to 0)
    ret = pouch_sem_take(&static_sem, POUCH_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Take again should fail (count is 0)
    ret = pouch_sem_take(&static_sem, POUCH_NO_WAIT);
    TEST_ASSERT_NOT_EQUAL(0, ret);
}

void run_unity_semaphore_tests(void)
{

    UNITY_BEGIN();

    RUN_TEST(test_sem_init_give_take);
    RUN_TEST(test_sem_reset);
    RUN_TEST(test_sem_static_define);

    UNITY_END();
}
