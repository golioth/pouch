/*
 * Copyright (c) 2025 Golioth, Inc.
 */

#include <pouch/transport/downlink.h>
#include <pouch/downlink.h>
#include <pouch/port.h>
#include <pouch/pouch.h>
#include <zephyr/ztest.h>

POUCH_LOG_REGISTER(downlink_test, POUCH_LOG_LEVEL_DBG);

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
    POUCH_LOG_DBG("Entry stream_id: %u", stream_id);
    POUCH_LOG_DBG("Entry path: %s", path);
    POUCH_LOG_DBG("Entry content_type: %u", content_type);

    zassert_str_equal(path, "/.s/lorem", "invalid path");
    zassert_equal(content_type, POUCH_CONTENT_TYPE_JSON, "invalid content_type");

    /* Initialize offset for received data */
    downlink_api.offset = 0;
}

static void downlink_data(unsigned int stream_id, const void *data, size_t len, bool is_last)
{
    POUCH_LOG_DBG("Entry stream_id: %u", stream_id);
    POUCH_LOG_DBG("Entry len: %zu", len);
    POUCH_LOG_DBG("Entry is_last: %d", (int) is_last);
    POUCH_LOG_HEXDUMP(data, len, "Entry data");

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

/* Plaintext header: [version=1, [0, "id123"]] */
static const uint8_t plaintext_header[] = {0x82, 0x01, 0x82, 0x00, 0x65, 'i', 'd', '1', '2', '3'};

/* SAEAD header: [1, [1, [[random bstr16], 0, 1, 10, bstr6], pouch_id=5]]. The mock crypto backend
 * rejects this with -ENOTSUP in crypto_downlink_start().
 */
static const uint8_t saead_header[] = {
    0x82, 0x01,                                                       /* header */
    0x83, 0x01,                                                       /* saead_info */
    0x85,                                                             /* session */
    0x81, 0x50, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, /* id */
    0x00,                                                             /* initiator */
    0x01,                                                             /* algorithm */
    0x0a,                                                             /* block size log */
    0x46, 1,    2, 3, 4, 5, 6,                                        /* cert_ref */
    0x05,                                                             /* pouch_id */
};

ZTEST(downlink, test_header_complete_plaintext)
{
    pouch_downlink_start();
    zassert_equal(pouch_downlink_push(plaintext_header, sizeof(plaintext_header)),
                  0,
                  "valid header should be accepted");
    pouch_downlink_finish();
}

ZTEST(downlink, test_header_incomplete_returns_zero)
{
    /* Every strict prefix of a valid header is incomplete, not broken */
    for (size_t len = 1; len < sizeof(plaintext_header); len++)
    {
        pouch_downlink_start();
        zassert_equal(pouch_downlink_push(plaintext_header, len),
                      0,
                      "incomplete header (%zu/%zu bytes) should return 0",
                      len,
                      sizeof(plaintext_header));
        pouch_downlink_finish();
    }
}

ZTEST(downlink, test_header_split_across_pushes)
{
    for (size_t split = 1; split < sizeof(plaintext_header); split++)
    {
        pouch_downlink_start();
        zassert_equal(pouch_downlink_push(plaintext_header, split),
                      0,
                      "first part, split %zu",
                      split);
        zassert_equal(
            pouch_downlink_push(plaintext_header + split, sizeof(plaintext_header) - split),
            0,
            "second part, split %zu",
            split);
        pouch_downlink_finish();
    }
}

ZTEST(downlink, test_header_broken_cbor_errors)
{
    /* Not an array */
    static const uint8_t not_array[] = {0xa1, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    /* Array of the right shape, but wrong element types */
    static const uint8_t wrong_types[] =
        {0x82, 0x61, 'x', 0x82, 0x00, 0x65, 'i', 'd', '1', '2', '3'};
    /* Unknown encryption type */
    static const uint8_t bad_enc_type[] = {0x82, 0x01, 0x82, 0x07, 0x65, 'i', 'd', '1', '2', '3'};

    static const struct
    {
        const uint8_t *data;
        size_t len;
        const char *name;
    } cases[] = {
        {not_array, sizeof(not_array), "not an array"},
        {wrong_types, sizeof(wrong_types), "wrong types"},
        {bad_enc_type, sizeof(bad_enc_type), "unknown encryption type"},
    };

    /* Corruption can only be told apart from an incomplete header once more data than the largest
     * possible header has arrived, so pad the broken headers past that.
     */
    for (size_t i = 0; i < ARRAY_SIZE(cases); i++)
    {
        uint8_t padded[2 * POUCH_DEVICE_ID_MAX_LEN + 64] = {0};

        zassert_true(cases[i].len <= sizeof(padded));
        memcpy(padded, cases[i].data, cases[i].len);

        pouch_downlink_start();
        zassert_true(pouch_downlink_push(padded, sizeof(padded)) < 0,
                     "broken header (%s) should return an error",
                     cases[i].name);
        pouch_downlink_finish();
    }
}

ZTEST(downlink, test_header_short_garbage_returns_zero)
{
    /* Too little data to rule out an incomplete header, so no error is reported yet */
    static const uint8_t garbage[] = {0xa1, 0x01, 0x02, 0x03};

    pouch_downlink_start();
    zassert_equal(pouch_downlink_push(garbage, sizeof(garbage)), 0);
    pouch_downlink_finish();
}

ZTEST(downlink, test_header_unsupported_version_errors)
{
    uint8_t header[sizeof(plaintext_header)];

    memcpy(header, plaintext_header, sizeof(header));
    header[1] = 0x02; /* version 2 */

    pouch_downlink_start();
    zassert_true(pouch_downlink_push(header, sizeof(header)) < 0,
                 "unsupported header version should return an error");
    pouch_downlink_finish();
}

ZTEST(downlink, test_header_crypto_start_failure_errors)
{
    pouch_downlink_start();
    zassert_equal(pouch_downlink_push(saead_header, sizeof(saead_header)),
                  -ENOTSUP,
                  "crypto_downlink_start() failure should be propagated");
    pouch_downlink_finish();
}
