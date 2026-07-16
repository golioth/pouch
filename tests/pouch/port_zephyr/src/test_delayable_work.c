/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <pouch/port.h>

struct test_work_ctx
{
    pouch_work_delayable_t dwork;
    volatile int call_count;
};

static void test_handler(pouch_work_delayable_t *dwork)
{
    struct test_work_ctx *ctx = CONTAINER_OF(dwork, struct test_work_ctx, dwork);
    ctx->call_count++;
}

ZTEST(delayable_work, test_delayable_work_init)
{
    static struct test_work_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_handler);

    /* Init should not invoke the handler */
    zassert_equal(ctx.call_count, 0, "Handler should not be called on init");
}

ZTEST(delayable_work, test_delayable_work_schedule_no_wait)
{
    static struct test_work_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_handler);

    int ret = pouch_work_schedule(&ctx.dwork, POUCH_NO_WAIT);
    zassert_equal(ret, 0, "pouch_work_schedule failed");

    /* Give the system workqueue time to process */
    k_sleep(K_MSEC(10));

    zassert_equal(ctx.call_count, 1, "Handler should have been called once");
}

ZTEST(delayable_work, test_delayable_work_schedule_delayed)
{
    static struct test_work_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_handler);

    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(50));
    zassert_equal(ret, 0, "pouch_work_schedule failed");

    /* Should not have fired yet */
    k_sleep(K_MSEC(10));
    zassert_equal(ctx.call_count, 0, "Handler should not be called before delay expires");

    /* Wait for it to fire */
    k_sleep(K_MSEC(60));
    zassert_equal(ctx.call_count, 1, "Handler should have been called after delay");
}

ZTEST(delayable_work, test_delayable_work_reschedule)
{
    static struct test_work_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_handler);

    /* Schedule with a long delay */
    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(500));
    zassert_equal(ret, 0, "pouch_work_schedule failed");

    /* Reschedule with no wait - should cancel the long delay and fire immediately */
    ret = pouch_work_reschedule(&ctx.dwork, POUCH_NO_WAIT);
    zassert_equal(ret, 0, "pouch_work_reschedule failed");

    k_sleep(K_MSEC(10));
    zassert_equal(ctx.call_count, 1, "Handler should have been called after reschedule");
}

ZTEST(delayable_work, test_delayable_work_cancel)
{
    static struct test_work_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_handler);

    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(30));
    zassert_equal(ret, 0, "pouch_work_schedule failed");

    /* Cancel before it fires */
    pouch_work_cancel_delayable(&ctx.dwork);

    /* Sleep past the original delay */
    k_sleep(K_MSEC(60));
    zassert_equal(ctx.call_count, 0, "Handler should not be called after cancel");
}

ZTEST(delayable_work, test_delayable_work_schedule_multiple)
{
    static struct test_work_ctx ctx = {.call_count = 0};

    pouch_work_delayable_init(&ctx.dwork, test_handler);

    /* Schedule twice before it fires */
    int ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(30));
    zassert_equal(ret, 0, "first pouch_work_schedule failed");

    ret = pouch_work_schedule(&ctx.dwork, POUCH_MSEC(30));
    zassert_equal(ret, 0, "second pouch_work_schedule failed");

    /* Wait for it to fire */
    k_sleep(K_MSEC(60));
    zassert_equal(ctx.call_count, 1, "Handler should be called exactly once");
}

ZTEST_SUITE(delayable_work, NULL, NULL, NULL, NULL, NULL);
