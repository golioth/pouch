/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "pouch/port.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct test_dwork_ctx
{
    pouch_work_delayable_t dwork;
    volatile int call_count;
};

static void test_dwork_handler(pouch_work_delayable_t *dwork)
{
    struct test_dwork_ctx *ctx = CONTAINER_OF(dwork, struct test_dwork_ctx, dwork);
    ctx->call_count++;
}

void test_delayable_work_init(void)
{
    static struct test_dwork_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    /* Init should not invoke the handler */
    TEST_ASSERT_EQUAL_INT(0, ctx.call_count);
}

void test_delayable_work_schedule_no_wait(void)
{
    static struct test_dwork_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    int ret = pouch_work_schedule(&ctx.dwork, POUCH_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Give the timer service task time to process */
    vTaskDelay(pdMS_TO_TICKS(10));

    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);
}

void test_delayable_work_schedule_delayed(void)
{
    static struct test_dwork_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(50));
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Should not have fired yet */
    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL_INT(0, ctx.call_count);

    /* Wait for it to fire */
    vTaskDelay(pdMS_TO_TICKS(60));
    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);
}

void test_delayable_work_reschedule(void)
{
    static struct test_dwork_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    /* Schedule with a long delay */
    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(500));
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Reschedule with no wait - should cancel the long delay and fire immediately */
    ret = pouch_work_reschedule(&ctx.dwork, POUCH_NO_WAIT);
    TEST_ASSERT_EQUAL_INT(0, ret);

    vTaskDelay(pdMS_TO_TICKS(10));
    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);
}

void test_delayable_work_cancel(void)
{
    static struct test_dwork_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(30));
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Cancel before it fires */
    pouch_work_cancel_delayable(&ctx.dwork);

    /* Sleep past the original delay */
    vTaskDelay(pdMS_TO_TICKS(60));
    TEST_ASSERT_EQUAL_INT(0, ctx.call_count);
}

void test_delayable_work_schedule_multiple(void)
{
    static struct test_dwork_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    /* Schedule twice before it fires */
    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(30));
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(30));
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Wait for it to fire */
    vTaskDelay(pdMS_TO_TICKS(60));
    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);
}

void test_delayable_work_reinit(void)
{
    struct test_dwork_ctx ctx = {.call_count = 0};

    /* First init + schedule + fire */
    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);
    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(20));
    TEST_ASSERT_EQUAL_INT(0, ret);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL_INT(1, ctx.call_count);

    /* Re-init on the same work item (must not orphan the old timer) */
    pouch_work_delayable_init(&ctx.dwork, test_dwork_handler);

    /* Schedule again — if the timer was orphaned, the daemon's list is
     * corrupted and this may crash or fail to fire. */
    ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(20));
    TEST_ASSERT_EQUAL_INT(0, ret);
    vTaskDelay(pdMS_TO_TICKS(50));
    TEST_ASSERT_EQUAL_INT(2, ctx.call_count);
}

TEST_CASE("Delayable Work", "[pouch][dwork]")
{
    RUN_TEST(test_delayable_work_init);
    RUN_TEST(test_delayable_work_schedule_no_wait);
    RUN_TEST(test_delayable_work_schedule_delayed);
    RUN_TEST(test_delayable_work_reschedule);
    RUN_TEST(test_delayable_work_cancel);
    RUN_TEST(test_delayable_work_schedule_multiple);
    RUN_TEST(test_delayable_work_reinit);
}
