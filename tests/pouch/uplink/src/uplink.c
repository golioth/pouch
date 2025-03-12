/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zcbor_decode.h>
#include <stdlib.h>

#include "mocks/transport.h"

#include <pouch/uplink.h>
#include <pouch/pouch.h>

#define DEVICE_ID "test-device-id"

static const struct pouch_config pouch_config = {
    .encryption_type = POUCH_ENCRYPTION_PLAINTEXT,
    .encryption.plaintext.device_id = DEVICE_ID,
};

static void *init_pouch(void)
{
    pouch_init(&pouch_config);
    return NULL;
}

ZTEST_SUITE(uplink, NULL, init_pouch, NULL, transport_reset, NULL);

K_SEM_DEFINE(write_done, 0, UINT16_MAX);

static size_t write_data_len;
static bool write_data_expect_fail;

static void write_to_uplink(struct k_work *work)
{
    size_t len = write_data_len;
    uint8_t *data = malloc(len);
    zassert_not_null(data);
    memset(data, 'a', len);

    int err = pouch_uplink_entry_write("test/path",
                                       POUCH_CONTENT_TYPE_OCTET_STREAM,
                                       data,
                                       len,
                                       K_SECONDS(1));
    if (write_data_expect_fail)
    {
        zassert_not_ok(err, "expected error, got %d", err);
    }
    else
    {
        zassert_ok(err, "expected success, got %d", err);
    }

    free(data);
    k_sem_give(&write_done);
}

K_WORK_DEFINE(write_data, write_to_uplink);

static int write_entry(size_t len, k_timeout_t timeout)
{
    write_data_len = len;
    write_data_expect_fail = false;
    k_work_submit(&write_data);
    return k_sem_take(&write_done, timeout);
}

static size_t read_data(uint8_t **data, size_t len)
{
    static uint8_t buf[CONFIG_POUCH_BLOCK_SIZE];

    transport_pull_data(buf, &len);

    *data = buf;

    return len;
}

ZTEST(uplink, test_pouch_header)
{
    transport_session_start();

    // write some data to create a pouch
    zassert_ok(write_entry(1, K_FOREVER));

    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);

    ZCBOR_STATE_D(zsd, 2, buf, len, 1, 0);

    zassert_true(zcbor_list_start_decode(zsd));

    uint32_t pouch_header_version;
    zassert_true(zcbor_uint32_decode(zsd, &pouch_header_version));
    zassert_equal(pouch_header_version,
                  1,
                  "Unexpected pouch header version %d",
                  pouch_header_version);

    uint32_t encryption_type;
    zassert_true(zcbor_list_start_decode(zsd));

    zassert_true(zcbor_uint32_decode(zsd, &encryption_type));
    zassert_equal(encryption_type, 0, "Unexpected encryption type %d", encryption_type);

    struct zcbor_string string = {0};
    zassert_true(zcbor_tstr_decode(zsd, &string));
    zassert_equal(string.len, strlen(DEVICE_ID), "Unexpected device ID length %d", string.len);
    zassert_str_equal(string.value, DEVICE_ID);

    zassert_true(zcbor_list_end_decode(zsd));

    zassert_true(zcbor_list_end_decode(zsd));
}

ZTEST(uplink, test_pouch_block)
{
    const char *path = "test/path";
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    transport_session_start();

    zassert_ok(pouch_uplink_entry_write(path,
                                        POUCH_CONTENT_TYPE_OCTET_STREAM,
                                        data,
                                        sizeof(data),
                                        K_FOREVER));
    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);

    // skip the pouch header:
    ZCBOR_STATE_D(zsd, 2, buf, len, 1, 0);

    zassert_true(zcbor_list_start_decode(zsd));
    while (!zcbor_array_at_end(zsd))
    {
        zcbor_any_skip(zsd, NULL);
    }
    zassert_true(zcbor_list_end_decode(zsd));

    uint8_t *block = (uint8_t *) zsd->payload;
    len -= (block - buf);

    int block_len = sys_get_be16(block);
    zassert_equal(block_len, len - 2, "Unexpected block length %d", block_len);

    zassert_equal(block[2], 0, "Expected block type to be ENTRY, was %u", block[2]);
}

ZTEST(uplink, test_pouch_entry)
{
    const char *path = "test/path";
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

    transport_session_start();

    zassert_ok(pouch_uplink_entry_write(path,
                                        POUCH_CONTENT_TYPE_OCTET_STREAM,
                                        data,
                                        sizeof(data),
                                        K_FOREVER));
    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);

    // skip the pouch header:
    ZCBOR_STATE_D(zsd, 2, buf, len, 1, 0);

    zassert_true(zcbor_list_start_decode(zsd));
    while (!zcbor_array_at_end(zsd))
    {
        zcbor_any_skip(zsd, NULL);
    }
    zassert_true(zcbor_list_end_decode(zsd));

    uint8_t *block = (uint8_t *) zsd->payload;
    uint8_t *block_data = &block[3];
    len -= (block_data - buf);

    zassert_equal(len, 5 + strlen(path) + sizeof(data), "Unexpected block length %d", len);

    uint16_t data_len = sys_get_be16(&block_data[0]);
    zassert_equal(data_len, sizeof(data), "Unexpected data length %d", data_len);

    uint16_t content_type = sys_get_be16(&block_data[2]);
    zassert_equal(content_type,
                  POUCH_CONTENT_TYPE_OCTET_STREAM,
                  "Unexpected content type %d",
                  content_type);

    uint8_t path_len = block_data[4];
    zassert_equal(path_len, strlen(path), "Unexpected path length %d", path_len);

    zassert_mem_equal(&block_data[5], path, path_len);

    zassert_mem_equal(&block_data[5 + path_len], data, sizeof(data));
}

ZTEST(uplink, test_pull_no_data)
{
    transport_session_start();

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_equal(len, 0, "expected to read 0 bytes, got %d", len);
}

ZTEST(uplink, test_pull_partial)
{
    transport_session_start();

    // write some data to create a pouch
    zassert_ok(write_entry(6, K_FOREVER));

    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    size_t len = CONFIG_POUCH_BLOCK_SIZE;
    uint8_t *buf = malloc(len);
    size_t offset = 0;
    while (offset < CONFIG_POUCH_BLOCK_SIZE)
    {
        len = 1;
        enum pouch_result result = transport_pull_data(&buf[offset], &len);

        zassert_equal(len, 1, "expected to read 1 byte, got %d", len);

        offset += len;

        if (result == POUCH_NO_MORE_DATA)
        {
            break;
        }

        zassert_equal(result, POUCH_MORE_DATA, "expected POUCH_MORE_DATA, got %d", result);
    }

    zassert_equal(offset, 42, "expected to read 42 bytes, got %d", offset);

    free(buf);
}

ZTEST(uplink, test_submit_before_session)
{
    zassert_ok(write_entry(6, K_FOREVER));

    transport_session_start();

    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_equal(len, 42);
}

ZTEST(uplink, test_submit_after_close)
{
    const char *path = "test/path";
    const uint8_t data1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t data2[] = {0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

    transport_session_start();

    zassert_ok(pouch_uplink_entry_write(path,
                                        POUCH_CONTENT_TYPE_OCTET_STREAM,
                                        data1,
                                        sizeof(data1),
                                        K_FOREVER));
    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    // it's okay to write more data after the close. It won't block.
    zassert_ok(pouch_uplink_entry_write(path,
                                        POUCH_CONTENT_TYPE_OCTET_STREAM,
                                        data2,
                                        sizeof(data2),
                                        K_FOREVER));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_equal(len, 42);  // not pulling the second entry

    transport_session_end();

    // new session:
    transport_session_start();

    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_equal(len, 42);  // just pulling the second entry
    zassert_mem_equal(&buf[42 - sizeof(data2)], data2, sizeof(data2));
}

ZTEST(uplink, test_multithread_writer)
{
    transport_session_start();
    // push some data to start a pouch
    zassert_ok(write_entry(10, K_MSEC(1)));

    // write another without blocking:
    int err = write_entry(20, K_NO_WAIT);
    zassert_equal(err, -EBUSY, "expected EBUSY, got %d", err);

    // Write from this thread while the other is blocked, should block and yield.
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    zassert_ok(pouch_uplink_entry_write("test/path",
                                        POUCH_CONTENT_TYPE_OCTET_STREAM,
                                        data,
                                        sizeof(data),
                                        K_MSEC(1)));
    // wait for the k_work to finish its write:
    k_sem_take(&write_done, K_FOREVER);

    zassert_ok(pouch_uplink_close(K_FOREVER));

    // let processing run:
    k_sleep(K_MSEC(1));

    /* Read out the data to make space in the ring buffer. Should return a full ring buffer's worth
     * of data.
     */
    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_true(len > 0);
}
