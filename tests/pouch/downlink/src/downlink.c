/*
 * Copyright (c) 2025 Golioth, Inc.
 */

#include <pouch/transport/downlink.h>
#include <pouch/downlink.h>
#include <pouch/pouch.h>
#include <zephyr/ztest.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink_test);

struct pouch_test_item_entry
{
    const char *path;
    uint16_t content_type;
    const uint8_t *data;
    size_t data_len;
};

struct pouch_test_item
{
    const uint8_t *data;
    size_t data_len;
    const struct pouch_test_item_entry *entries;
    size_t num_entries;
};

struct downlink_api_context
{
    const uint8_t *data;
    size_t len;
    size_t offset;
    size_t payload_len;

    const struct pouch_test_item_entry *entry;
};

static struct downlink_api_context downlink_api;

static const struct pouch_config pouch_config = {
    .device_id = CONFIG_POUCH_DEVICE_NAME,
};

static void *init_pouch(void)
{
    pouch_init(&pouch_config);
    return NULL;
}

ZTEST_SUITE(downlink, NULL, init_pouch, NULL, NULL, NULL);

static void downlink_start(unsigned int stream_id, const char *path, uint16_t content_type)
{
    LOG_DBG("Entry stream_id: %u", stream_id);
    LOG_DBG("Entry path: %s", path);
    LOG_DBG("Entry content_type: %u", content_type);

    zassert_str_equal(path, "/.s/lorem", "invalid path");
    zassert_equal(content_type, POUCH_CONTENT_TYPE_JSON, "invalid content_type");

    /* Initialize offset for received data */
    downlink_api.offset = 0;
}

static void downlink_data(unsigned int stream_id, const void *data, size_t len, bool is_last)
{
    LOG_DBG("Entry stream_id: %u", stream_id);
    LOG_DBG("Entry len: %zu", len);
    LOG_DBG("Entry is_last: %d", (int) is_last);
    LOG_HEXDUMP_DBG(data, len, "Entry data");

    zassert_mem_equal(data,
                      &downlink_api.entry->data[downlink_api.offset],
                      len,
                      "content is not as expected");

    downlink_api.offset += len;

    zassert_equal(is_last,
                  (downlink_api.offset >= downlink_api.entry->data_len),
                  "is_last is not as expected");

    if (is_last)
    {
        downlink_api.entry++;

        /* Check if all data was received */
        zassert_equal(downlink_api.offset,
                      downlink_api.payload_len,
                      "received offset does not match (%d %d)",
                      downlink_api.offset,
                      downlink_api.payload_len);
    }
}

POUCH_DOWNLINK_HANDLER(downlink_start, downlink_data);

static void pouch_downlink_push_all(const uint8_t *data, size_t len, size_t mtu)
{
    while (len)
    {
        size_t fragment_len = MIN(len, mtu);

        pouch_downlink_push(data, fragment_len);

        data += fragment_len;
        len -= fragment_len;
    }
}

static void test_lorem(const struct pouch_test_item *test_item)
{
    downlink_api.entry = &test_item->entries[0];

    downlink_api.data = test_item->data;
    downlink_api.len = test_item->data_len;
    downlink_api.payload_len = test_item->entries[0].data_len;

    pouch_downlink_start();
    pouch_downlink_push_all(test_item->data, test_item->data_len, CONFIG_POUCH_TRANSPORT_MTU);
    pouch_downlink_finish();

    /* Let all downlink messages be processed */
    k_sleep(K_MSEC(100));

    zassert_equal_ptr(downlink_api.entry,
                      &test_item->entries[test_item->num_entries],
                      "Number of last_count does not match number of messages within pouch");
}

#include "lorem-10-x1.c"

ZTEST(downlink, test_lorem_01_10_x1)
{
    test_lorem(&lorem_10_x1);
}

#include "lorem-512-x1.c"

ZTEST(downlink, test_lorem_02_512_x1)
{
    test_lorem(&lorem_512_x1);
}

#include "lorem-1024-x1.c"

ZTEST(downlink, test_lorem_03_1024_x1)
{
    test_lorem(&lorem_1024_x1);
}

#include "lorem-102400-x1.c"

ZTEST(downlink, test_lorem_04_102400_x1)
{
    test_lorem(&lorem_102400_x1);
}

#include "lorem-10-x5.c"

ZTEST(downlink, test_lorem_05_10_x5)
{
    test_lorem(&lorem_10_x5);
}

#include "lorem-100-x5.c"

ZTEST(downlink, test_lorem_06_100_x5)
{
    test_lorem(&lorem_100_x5);
}

#include "lorem-200-x5.c"

ZTEST(downlink, test_lorem_07_200_x5)
{
    test_lorem(&lorem_200_x5);
}

#include "lorem-500-x5.c"

ZTEST(downlink, test_lorem_08_500_x5)
{
    test_lorem(&lorem_500_x5);
}

#include "lorem-1024-x5.c"

ZTEST(downlink, test_lorem_09_1024_x5)
{
    test_lorem(&lorem_1024_x5);
}
