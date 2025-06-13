/*
 * Copyright (c) 2025 Golioth, Inc.
 */

#include <pouch/transport/downlink.h>
#include <pouch/types.h>
#include <zephyr/ztest.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(downlink_test);

#define BLOCK_SIZE 512

#define LEN_POUCH_HEADER 5
#define LEN_BLOCK_HEADER 3
#define LEN_ENTRY_LENGTH 2
#define LEN_ENTRY_PATH 2
#define LEN_ENTRY_CONTENT_TYPE 1

#define FIRST_BLOCK_OFFSET (LEN_POUCH_HEADER + sizeof("id123") - 1)

#define TRANSPORT_MTU 19

struct downlink_api_context
{
    const uint8_t *data;
    size_t len;
    size_t offset;
    size_t payload_offset;
};

static struct downlink_api_context downlink_api;

static void downlink_api_before(void *)
{
    /* Initialize offset for received data */
    downlink_api.offset = 0;
}

static void downlink_api_after(void *)
{
    /* Check if all data was received */
    zassert_equal(downlink_api.offset,
                  downlink_api.len - downlink_api.payload_offset,
                  "received content length does not match");
}

ZTEST_SUITE(downlink, NULL, NULL, downlink_api_before, downlink_api_after, NULL);

void downlink_received(const char *path,
                       uint16_t content_type,
                       const void *data,
                       size_t len,
                       size_t offset,
                       bool is_last)
{
    LOG_DBG("Entry path: %s", path);
    LOG_DBG("Entry content_type: %u", content_type);
    LOG_DBG("Entry data offset: %zu", offset);
    LOG_DBG("Entry is_last: %d", (int) is_last);
    LOG_HEXDUMP_DBG(data, len, "Entry data");

    zassert_str_equal(path, "/.s/lorem", "invalid path");
    zassert_equal(content_type, POUCH_CONTENT_TYPE_JSON, "invalid content_type");
    zassert_equal(offset,
                  downlink_api.offset,
                  "offset is not as expected (%zu %zu)",
                  offset,
                  downlink_api.offset);
    zassert_mem_equal(data,
                      &downlink_api.data[downlink_api.offset + downlink_api.payload_offset],
                      len,
                      "content is not as expected");

    downlink_api.offset += len;

    /*
     * Blocks are always BLOCK_SIZE (including block header), so make sure that
     * after each block we skip next block header in the "expected" pouch
     * binary.
     */
    if ((downlink_api.offset + downlink_api.payload_offset - FIRST_BLOCK_OFFSET) % BLOCK_SIZE == 0)
    {
        downlink_api.payload_offset += 3 /* block header */;
    }

    bool api_expected_last = downlink_api.offset + downlink_api.payload_offset >= downlink_api.len;

    zassert_equal(is_last, api_expected_last, "is_last is not as expected");
}

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

static void test_lorem(const uint8_t *data, size_t len, size_t payload_offset, size_t mtu)
{
    downlink_api.data = data;
    downlink_api.len = len;
    downlink_api.payload_offset = payload_offset;

    pouch_downlink_start();
    pouch_downlink_push_all(data, len, mtu);
    pouch_downlink_finish();
}

static const uint8_t lorem_10[] = {
#include "lorem-10.inc"
};

ZTEST(downlink, test_lorem_01_10)
{
    size_t payload_offset = FIRST_BLOCK_OFFSET + LEN_BLOCK_HEADER + LEN_ENTRY_LENGTH
        + LEN_ENTRY_PATH + LEN_ENTRY_CONTENT_TYPE + sizeof("/.s/lorem") - 1;

    test_lorem(lorem_10, ARRAY_SIZE(lorem_10), payload_offset, TRANSPORT_MTU);
}

static const uint8_t lorem_512[] = {
#include "lorem-512.inc"
};

ZTEST(downlink, test_lorem_02_512)
{
    size_t payload_offset = FIRST_BLOCK_OFFSET + LEN_BLOCK_HEADER + LEN_ENTRY_PATH
        + LEN_ENTRY_CONTENT_TYPE + sizeof("/.s/lorem") - 1;

    test_lorem(lorem_512, ARRAY_SIZE(lorem_512), payload_offset, TRANSPORT_MTU);
}

static const uint8_t lorem_1024[] = {
#include "lorem-1024.inc"
};

ZTEST(downlink, test_lorem_03_1024)
{
    size_t payload_offset = FIRST_BLOCK_OFFSET + LEN_BLOCK_HEADER + LEN_ENTRY_PATH
        + LEN_ENTRY_CONTENT_TYPE + sizeof("/.s/lorem") - 1;

    test_lorem(lorem_1024, ARRAY_SIZE(lorem_1024), payload_offset, TRANSPORT_MTU);
}

static const uint8_t lorem_102400[] = {
#include "lorem-102400.inc"
};

ZTEST(downlink, test_lorem_04_100k)
{
    size_t payload_offset = FIRST_BLOCK_OFFSET + LEN_BLOCK_HEADER + LEN_ENTRY_PATH
        + LEN_ENTRY_CONTENT_TYPE + sizeof("/.s/lorem") - 1;

    test_lorem(lorem_102400, ARRAY_SIZE(lorem_102400), payload_offset, TRANSPORT_MTU);
}
