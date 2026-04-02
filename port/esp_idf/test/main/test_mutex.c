#include "unity.h"
#include "pouch/port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static pouch_mutex_t test_mutex;
static volatile int shared_counter;

POUCH_MUTEX_DEFINE(static_mutex);

static void task_increment(void *param)
{
    int *increments = (int *) param;
    for (int i = 0; i < *increments; ++i)
    {
        TEST_ASSERT_TRUE_MESSAGE(pouch_mutex_lock(&test_mutex, 1000), "Failed to lock");
        shared_counter += 1;
        TEST_ASSERT_TRUE_MESSAGE(pouch_mutex_unlock(&test_mutex), "Failed to unlock");
    }
    vTaskDelete(NULL);
}

static void task_lock_timeout(void *param)
{
    TEST_ASSERT_FALSE_MESSAGE(pouch_mutex_lock(&test_mutex, 100), "Lock failed to time out");
    vTaskDelete(NULL);
}

void setUp(void)
{
    pouch_mutex_init(&test_mutex);
    TEST_ASSERT_NOT_NULL(test_mutex);
    shared_counter = 0;
}

void tearDown(void) {}

void test_mutex_init_and_lock_unlock(void)
{
    TEST_ASSERT_TRUE(pouch_mutex_lock(&test_mutex, 1000));
    TEST_ASSERT_TRUE(pouch_mutex_unlock(&test_mutex));
}

void test_mutex_protection_multithread(void)
{
    int err;
    int increments = 1000;
    err = xTaskCreate(task_increment, "T1", 2048, &increments, 5, NULL);
    TEST_ASSERT_EQUAL(pdPASS, err);
    err = xTaskCreate(task_increment, "T2", 2048, &increments, 5, NULL);
    TEST_ASSERT_EQUAL(pdPASS, err);

    // Wait for tasks to finish
    vTaskDelay(pdMS_TO_TICKS(2000));

    TEST_ASSERT_EQUAL(increments * 2, shared_counter);
}

void test_mutex_timeout_multithread(void)
{
    TEST_ASSERT_TRUE_MESSAGE(pouch_mutex_lock(&test_mutex, 1000), "Failed to lock");
    int err = xTaskCreate(task_lock_timeout, "T3", 2048, NULL, 5, NULL);
    TEST_ASSERT_EQUAL(pdPASS, err);

    // Wait for tasks to finish
    vTaskDelay(pdMS_TO_TICKS(500));

    TEST_ASSERT_TRUE_MESSAGE(pouch_mutex_unlock(&test_mutex), "Failed to unlock");
}

void test_mutex_static_define(void)
{
    TEST_ASSERT_NOT_NULL(static_mutex);
    TEST_ASSERT_TRUE(pouch_mutex_lock(&static_mutex, 1000));
    TEST_ASSERT_TRUE(pouch_mutex_unlock(&static_mutex));
}

int run_unity_mutex_tests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutex_init_and_lock_unlock);
    RUN_TEST(test_mutex_protection_multithread);
    RUN_TEST(test_mutex_timeout_multithread);
    RUN_TEST(test_mutex_static_define);
    return UNITY_END();
}
