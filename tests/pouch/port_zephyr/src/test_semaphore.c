#include <zephyr/ztest.h>
#include <pouch/port.h>

#define SEM_INIT_COUNT 1
#define SEM_LIMIT 2

POUCH_SEM_DEFINE(static_sem, 0, 1);

ZTEST(semaphore_api, test_sem_init_give_take)
{
    pouch_sem_t sem;
    int ret;

    /* Initialize semaphore */
    ret = pouch_sem_init(&sem, SEM_INIT_COUNT, SEM_LIMIT);
    zassert_equal(ret, 0, "pouch_sem_init failed");

    /* Take should succeed (count goes from 1 to 0) */
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    zassert_equal(ret, 0, "pouch_sem_take failed");

    /* Take again should fail (count is 0) */
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    zassert_not_equal(ret, 0, "pouch_sem_take should fail when count is 0");

    /* Give should increment count */
    pouch_sem_give(&sem);

    /* Take should succeed again */
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    zassert_equal(ret, 0, "pouch_sem_take after give failed");
}

ZTEST(semaphore_api, test_sem_reset)
{
    pouch_sem_t sem;
    int ret;

    ret = pouch_sem_init(&sem, 0, SEM_LIMIT);
    zassert_equal(ret, 0, "pouch_sem_init failed");

    /* Give to increment count */
    pouch_sem_give(&sem);

    /* Reset should set count to 0 */
    pouch_sem_reset(&sem);

    /* Take should fail (count is 0) */
    ret = pouch_sem_take(&sem, POUCH_NO_WAIT);
    zassert_not_equal(ret, 0, "pouch_sem_take should fail after reset");
}

ZTEST(semaphore_api, test_sem_static_define)
{
    int ret;

    /* Give to increment count from 0 to 1 */
    pouch_sem_give(&static_sem);

    /* Take should succeed (count goes from 1 to 0) */
    ret = pouch_sem_take(&static_sem, POUCH_NO_WAIT);
    zassert_equal(ret, 0, "pouch_sem_take failed");

    /* Take again should fail (count is 0) */
    ret = pouch_sem_take(&static_sem, POUCH_NO_WAIT);
    zassert_not_equal(ret, 0, "pouch_sem_take should fail when count is 0");
}

ZTEST_SUITE(semaphore_api, NULL, NULL, NULL, NULL, NULL);
