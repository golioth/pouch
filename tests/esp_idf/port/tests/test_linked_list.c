#include "unity.h"
#include "pouch/port.h"
#include <stdlib.h>

typedef struct test_node
{
    int value;
    pouch_slist_node_t node;
} test_node_t;

static test_node_t *make_node(int value)
{
    test_node_t *n = malloc(sizeof(test_node_t));
    n->value = value;
    pouch_slist_node_init(&n->node);
    return n;
}

static void free_node(test_node_t *n)
{
    free(n);
}

/* Do not define setUP() or tearDown() as they're defined in test_mutex.c */

void test_slist_init_and_append(void)
{
    pouch_slist_t list;
    pouch_slist_init(&list);

    test_node_t *n1 = make_node(1);
    test_node_t *n2 = make_node(2);

    pouch_slist_append(&list, &n1->node);
    pouch_slist_append(&list, &n2->node);

    pouch_slist_node_t *head = pouch_slist_get(&list);
    TEST_ASSERT_NOT_NULL(head);
    test_node_t *got1 = CONTAINER_OF(head, test_node_t, node);
    TEST_ASSERT_EQUAL_INT(1, got1->value);

    head = pouch_slist_get(&list);
    TEST_ASSERT_NOT_NULL(head);
    test_node_t *got2 = CONTAINER_OF(head, test_node_t, node);
    TEST_ASSERT_EQUAL_INT(2, got2->value);

    head = pouch_slist_get(&list);
    TEST_ASSERT_NULL(head);

    free_node(got1);
    free_node(got2);
}

void test_slist_peek_head(void)
{
    pouch_slist_t list;
    pouch_slist_init(&list);

    test_node_t *n1 = make_node(10);
    pouch_slist_append(&list, &n1->node);

    pouch_slist_node_t *head = pouch_slist_peek_head(&list);
    TEST_ASSERT_NOT_NULL(head);
    test_node_t *got = CONTAINER_OF(head, test_node_t, node);
    TEST_ASSERT_EQUAL_INT(10, got->value);

    // Head should still be present after peek
    head = pouch_slist_peek_head(&list);
    TEST_ASSERT_NOT_NULL(head);

    // Remove head
    head = pouch_slist_get(&list);
    TEST_ASSERT_NOT_NULL(head);

    // Now list should be empty
    head = pouch_slist_peek_head(&list);
    TEST_ASSERT_NULL(head);

    free_node(got);
}

int run_unity_linked_list_tests(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_slist_init_and_append);
    RUN_TEST(test_slist_peek_head);
    return UNITY_END();
}
