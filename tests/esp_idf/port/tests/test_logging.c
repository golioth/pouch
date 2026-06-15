#include "unity.h"
#include <pouch/port.h>
#include <esp_log.h>
#include <string.h>
#include <stdarg.h>

POUCH_LOG_REGISTER(test_logging, POUCH_LOG_LEVEL_DBG);

static char capture_buf[1024];
static int capture_len;
static vprintf_like_t original_vprintf;

static int capture_vprintf(const char *fmt, va_list args)
{
    int remaining = sizeof(capture_buf) - capture_len - 1;
    if (remaining <= 0)
    {
        return 0;
    }
    int ret = vsnprintf(capture_buf + capture_len, remaining, fmt, args);
    if (ret > 0)
    {
        capture_len += ret;
    }
    return ret;
}

static void capture_begin(void)
{
    memset(capture_buf, 0, sizeof(capture_buf));
    capture_len = 0;
    original_vprintf = esp_log_set_vprintf(capture_vprintf);
}

static void capture_end(void)
{
    esp_log_set_vprintf(original_vprintf);
}

void test_log_error(void)
{
    capture_begin();
    POUCH_LOG_ERR("error code %d", 42);
    capture_end();

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "error code 42"),
                                 "ERR log should contain error code 42");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "E ("),
                                 "ERR log should contain E ( level indicator");
}

void test_log_warning(void)
{
    capture_begin();
    POUCH_LOG_WRN("warning %s", "message");
    capture_end();

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "warning message"),
                                 "WRN log should contain warning message");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "W ("),
                                 "WRN log should contain W ( level indicator");
}

void test_log_info(void)
{
    capture_begin();
    POUCH_LOG_INF("info value %u", 123u);
    capture_end();

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "info value 123"),
                                 "INF log should contain info value 123");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "I ("),
                                 "INF log should contain I ( level indicator");
}

void test_log_tag(void)
{
    capture_begin();
    POUCH_LOG_INF("tag check");
    capture_end();

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "test_logging"),
                                 "Log output should contain the registered tag");
}

void test_log_hexdump(void)
{
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    esp_log_level_set("test_logging", ESP_LOG_DEBUG);

    capture_begin();
    POUCH_LOG_DBG("debug value %d", 99);
    POUCH_LOG_HEXDUMP(data, sizeof(data), "hex test");
    capture_end();

    esp_log_level_set("test_logging", ESP_LOG_INFO);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "debug value 99"),
                                 "DBG log should contain debug value 99");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "hex test"),
                                 "Hexdump label should appear in output");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(capture_buf, "de ad be ef"),
                                 "Hexdump should contain hex bytes");
}

TEST_CASE("Logging", "[pouch][logging]")
{
    RUN_TEST(test_log_error);
    RUN_TEST(test_log_warning);
    RUN_TEST(test_log_info);
    RUN_TEST(test_log_tag);
    RUN_TEST(test_log_hexdump);
}
