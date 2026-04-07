#include "unity.h"
#include "pouch/port.h"
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/projdefs.h>

#define WORKQ_PRIO 4
#define WORKQ_STACK_SIZE 1024
pouch_atomic_t work_counter;

pouch_work_q_t basic_workq;
POUCH_THREAD_STACK_DEFINE(basic_workq_stack, WORKQ_STACK_SIZE);

static void test_work_handler(pouch_work_t *work)
{
    /* For submit twice test, ensure first work doesn't complete before duplicate is submitted */
    vTaskDelay(pdMS_TO_TICKS(10));

    pouch_atomic_inc(&work_counter);
}

void test_pouch_work_init_and_submit(void)
{
    pouch_work_t work1, work2;
    pouch_atomic_clear(&work_counter);

    // Initialize work queue
    pouch_work_queue_init(&basic_workq);

    // Start work queue thread
    pouch_work_queue_start(&basic_workq,
                           basic_workq_stack,
                           WORKQ_STACK_SIZE,
                           WORKQ_PRIO,
                           "test_workq");

    // Initialize work items
    pouch_work_init(&work1, test_work_handler);
    pouch_work_init(&work2, test_work_handler);

    // Submit work items
    TEST_ASSERT_EQUAL_INT(1, pouch_work_submit_to_queue(&basic_workq, &work1));
    TEST_ASSERT_EQUAL_INT(1, pouch_work_submit_to_queue(&basic_workq, &work2));

    // Wait for work to be processed
    int retries = 10;
    while (work_counter < 2 && retries-- > 0)
    {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    TEST_ASSERT_EQUAL_INT(2, pouch_atomic_get_value(&work_counter));
}

pouch_work_q_t dupe_workq;
POUCH_THREAD_STACK_DEFINE(dupe_workq_stack, WORKQ_STACK_SIZE);

void test_pouch_work_submit_twice(void)
{
    pouch_work_t work;
    pouch_atomic_clear(&work_counter);

    // Initialize work queue
    pouch_work_queue_init(&dupe_workq);

    // Start work queue thread
    pouch_work_queue_start(&dupe_workq,
                           dupe_workq_stack,
                           WORKQ_STACK_SIZE,
                           WORKQ_PRIO,
                           "dupe_workq");

    // Initialize work item
    pouch_work_init(&work, test_work_handler);

    // Submit work item once
    TEST_ASSERT_EQUAL_INT(1, pouch_work_submit_to_queue(&dupe_workq, &work));

    // Yield to ensure work starts
    pouch_yield();

    /* (Re)submit the work to the queue. Flag was cleared at start of work. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1,
                                  pouch_work_submit_to_queue(&dupe_workq, &work),
                                  "Expected work to be queued.");

    /* Initial work should still be processing (blocking the workq thread) due to the delay */
    /* Resubmission is already in the queue, so we expect an "already queued" return code of 1 */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0,
                                  pouch_work_submit_to_queue(&dupe_workq, &work),
                                  "Expected work to already be queued.");

    /* Wait for all work to be processed */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Verify new value from two work submissions */
    TEST_ASSERT_EQUAL_INT(2, pouch_atomic_get_value(&work_counter));

    /* Test that pending flag was cleared and work can be processed */
    TEST_ASSERT_EQUAL_INT(1, pouch_work_submit_to_queue(&dupe_workq, &work));
    vTaskDelay(pdMS_TO_TICKS(20));
    TEST_ASSERT_EQUAL_INT(3, pouch_atomic_get_value(&work_counter));
}

pouch_work_q_t multi_workq1, multi_workq2;
POUCH_THREAD_STACK_DEFINE(multi_workq1_stack, WORKQ_STACK_SIZE);
POUCH_THREAD_STACK_DEFINE(multi_workq2_stack, WORKQ_STACK_SIZE);

void test_pouch_work_multiple_queues(void)
{
    pouch_work_t work1, work2;
    pouch_atomic_clear(&work_counter);

    pouch_work_queue_init(&multi_workq1);
    pouch_work_queue_start(&multi_workq1,
                           multi_workq1_stack,
                           WORKQ_STACK_SIZE,
                           WORKQ_PRIO,
                           "multi_workq1");

    pouch_work_queue_init(&multi_workq2);
    pouch_work_queue_start(&multi_workq2,
                           multi_workq2_stack,
                           WORKQ_STACK_SIZE,
                           WORKQ_PRIO,
                           "multi_workq2");

    // Initialize work items
    pouch_work_init(&work1, test_work_handler);
    pouch_work_init(&work2, test_work_handler);

    // Submit work item to queues
    TEST_ASSERT_EQUAL_INT(1, pouch_work_submit_to_queue(&multi_workq1, &work1));
    TEST_ASSERT_EQUAL_INT(1, pouch_work_submit_to_queue(&multi_workq2, &work2));

    /* Wait for work to be processed */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Verify new value */
    TEST_ASSERT_EQUAL_INT(2, pouch_atomic_get_value(&work_counter));

    /* multi_workq1 and multi_workq2 are about to go out of scope, delete tasks */
    vTaskDelete(multi_workq1.handle);
    vTaskDelete(multi_workq2.handle);
}

void run_unity_work_queue_tests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pouch_work_init_and_submit);
    RUN_TEST(test_pouch_work_submit_twice);
    RUN_TEST(test_pouch_work_multiple_queues);
    UNITY_END();
}
