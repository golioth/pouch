#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "unity.h"
#include <pouch/port.h>
#include <limits.h>

void test_atomic_rollover(void)
{
    pouch_atomic_t val;

    // Test Maximum Rollover
    pouch_atomic_set(&val, INT_MAX);
    pouch_atomic_inc(&val);
    // Standard two's complement wrap-around
    TEST_ASSERT_EQUAL_INT(INT_MIN, pouch_atomic_get(&val));
}

void test_atomic_negative_boundary(void)
{
    pouch_atomic_t val;
    pouch_atomic_set(&val, -1);
    pouch_atomic_inc(&val);

    TEST_ASSERT_EQUAL_INT32(0, pouch_atomic_get(&val));
}

void test_atomic_negative_math(void)
{
    pouch_atomic_t val;

    pouch_atomic_set(&val, 0);
    pouch_atomic_dec(&val);
    TEST_ASSERT_EQUAL_INT(-1, pouch_atomic_get(&val));

    pouch_atomic_inc(&val);
    TEST_ASSERT_EQUAL_INT(0, pouch_atomic_get(&val));
}

#define ITERATIONS_PER_TASK 10000
#define NUM_TASKS 2

// Structure to pass data to tasks
typedef struct
{
    pouch_atomic_t *atomic_var;
    SemaphoreHandle_t done_sem;
} test_ctx_t;

// The worker task
void atomic_worker_task(void *pvParameters)
{
    test_ctx_t *ctx = (test_ctx_t *) pvParameters;

    for (int i = 0; i < ITERATIONS_PER_TASK; i++)
    {
        pouch_atomic_inc(ctx->atomic_var);
    }

    // Signal completion
    xSemaphoreGive(ctx->done_sem);
    vTaskDelete(NULL);
}

void test_atomic_concurrency(void)
{
    pouch_atomic_t counter;
    pouch_atomic_set(&counter, 0);

    SemaphoreHandle_t done_sem = xSemaphoreCreateCounting(NUM_TASKS, 0);
    test_ctx_t context = {.atomic_var = &counter, .done_sem = done_sem};

    // Spin up Task A on Core 0
    xTaskCreatePinnedToCore(atomic_worker_task, "worker_a", 2048, &context, 5, NULL, 0);

    // Spin up Task B on Core 1 (True parallel execution)
    xTaskCreatePinnedToCore(atomic_worker_task, "worker_b", 2048, &context, 5, NULL, 1);

    // Wait for both to finish (timeout after 1 second)
    for (int i = 0; i < NUM_TASKS; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(xSemaphoreTake(done_sem, pdMS_TO_TICKS(1000)), "Task timed out!");
    }

    // Verify the result
    int32_t final_val = pouch_atomic_get(&counter);
    printf("Final Atomic Value: %ld\n", final_val);

    TEST_ASSERT_EQUAL_INT32(ITERATIONS_PER_TASK * NUM_TASKS, final_val);

    vSemaphoreDelete(done_sem);
}
#define BIT_TO_TEST 12
static pouch_atomic_t shared_flags[DIV_ROUND_UP(32, POUCH_ATOMIC_BITS)];
static pouch_atomic_t success_count;

void bit_worker_task(void *pvParameters)
{
    SemaphoreHandle_t done_sem = (SemaphoreHandle_t) pvParameters;

    // Attempt to set the bit. test_and_set returns the PREVIOUS value.
    // If it returns false, it means we were the first to flip it from 0 to 1.
    if (pouch_atomic_test_and_set_bit(shared_flags, BIT_TO_TEST) == false)
    {
        pouch_atomic_inc(&success_count);
    }

    xSemaphoreGive(done_sem);
    vTaskDelete(NULL);
}
void test_atomic_bit_basic_set_clear(void)
{
    POUCH_ATOMIC_DEFINE(flags, 1);

    // Test Set
    pouch_atomic_set_bit(flags, 0);
    TEST_ASSERT_TRUE(pouch_atomic_test_bit(flags, 0));

    // Test Clear
    pouch_atomic_clear_bit(flags, 0);
    TEST_ASSERT_FALSE(pouch_atomic_test_bit(flags, 0));
}

void test_atomic_bit_array_spanning(void)
{
    // Define enough bits to span at least 3 underlying pouch_atomic_t elements
    const int total_bits = POUCH_ATOMIC_BITS * 3;
    POUCH_ATOMIC_DEFINE(large_mask, total_bits);

    // Clear all initially (manually or via a helper if available)
    for (int i = 0; i < total_bits; i++)
        pouch_atomic_clear_bit(large_mask, i);

    // Set a bit at the boundary of the second element
    int target_bit = POUCH_ATOMIC_BITS + 5;
    pouch_atomic_set_bit(large_mask, target_bit);

    TEST_ASSERT_TRUE(pouch_atomic_test_bit(large_mask, target_bit));
    TEST_ASSERT_FALSE(pouch_atomic_test_bit(large_mask, target_bit - 1));
    TEST_ASSERT_FALSE(pouch_atomic_test_bit(large_mask, target_bit + 1));
}

void test_atomic_test_and_set_logic(void)
{
    POUCH_ATOMIC_DEFINE(flags, 10);
    pouch_atomic_clear_bit(flags, 5);

    // First time: should be false (was 0), but now becomes true (1)
    TEST_ASSERT_FALSE(pouch_atomic_test_and_set_bit(flags, 5));
    TEST_ASSERT_TRUE(pouch_atomic_test_bit(flags, 5));

    // Second time: should be true (was 1), remains true
    TEST_ASSERT_TRUE(pouch_atomic_test_and_set_bit(flags, 5));
}

void test_atomic_bit_concurrency(void)
{
    pouch_atomic_set(&success_count, 0);
    // Clear our target bit
    pouch_atomic_clear_bit(shared_flags, BIT_TO_TEST);

    SemaphoreHandle_t done_sem = xSemaphoreCreateCounting(10, 0);

    // Create 10 tasks all fighting for the same bit
    for (int i = 0; i < 10; i++)
    {
        xTaskCreate(bit_worker_task, "bit_worker", 2048, done_sem, 10, NULL);
    }

    for (int i = 0; i < 10; i++)
    {
        xSemaphoreTake(done_sem, pdMS_TO_TICKS(1000));
    }

    // Only EXACTLY one task should have successfully "won" the bit
    TEST_ASSERT_EQUAL_INT32(1, pouch_atomic_get(&success_count));
    TEST_ASSERT_TRUE(pouch_atomic_test_bit(shared_flags, BIT_TO_TEST));

    vSemaphoreDelete(done_sem);
}

void run_atomic_tests(void)
{

    UNITY_BEGIN();

    RUN_TEST(test_atomic_rollover);
    RUN_TEST(test_atomic_negative_boundary);
    RUN_TEST(test_atomic_negative_math);
    RUN_TEST(test_atomic_concurrency);

    RUN_TEST(test_atomic_bit_basic_set_clear);
    RUN_TEST(test_atomic_bit_array_spanning);
    RUN_TEST(test_atomic_test_and_set_logic);
    RUN_TEST(test_atomic_bit_concurrency);

    UNITY_END();
}
