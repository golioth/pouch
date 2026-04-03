#include "test_linked_list.h"
#include "test_msgq.h"
#include "test_mutex.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void run_unity_tests(void)
{
    run_unity_linked_list_tests();
    run_unity_msgq_tests();
    run_unity_mutex_tests();
}
