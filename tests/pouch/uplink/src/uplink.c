/*
 * Copyright (c) 2025 Golioth, Inc.
 */
#include <zephyr/ztest.h>
#include <zephyr/sys/byteorder.h>
#include <zcbor_decode.h>
#include <stdlib.h>
#include <stdio.h>
#include "mocks/transport.h"
#include "utils.h"

#include <pouch/uplink.h>
#include <pouch/pouch.h>

#define DEVICE_ID "test-device-id"

static const struct pouch_config pouch_config = {
    .device_id = DEVICE_ID,
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
    uint8_t *block = skip_pouch_header(buf, &len);

    int block_len = sys_get_be16(block);
    zassert_equal(block_len, len - 2, "Unexpected block length %d", block_len);

    zassert_equal(block[2],
                  0x80 | 0x40,
                  "Expected block type to be ENTRY and no more data to be true, was %u",
                  block[2]);
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

    uint8_t *block = skip_pouch_header(buf, &len);
    uint8_t *block_data = &block[3];
    len -= 3;

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

ZTEST(uplink, test_stream_basic)
{
    transport_session_start();

    struct pouch_stream *stream =
        pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream, "Failed to open stream");

    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    size_t written = pouch_stream_write(stream, (void *) data, sizeof(data), K_NO_WAIT);
    zassert_equal(written, sizeof(data), "Unexpected write length %d", written);

    zassert_ok(pouch_stream_close(stream, K_NO_WAIT));

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);

    uint8_t *block = skip_pouch_header(buf, &len);

    size_t block_len = sys_get_be16(block);
    zassert_equal(block_len, len - 2, "Unexpected block length %d", block_len);

    // 0x81 is the first stream ID for a closed stream:
    uint8_t stream_id = block[2];
    zassert_between_inclusive(stream_id, 0x81, 0xff, "Unexpected stream ID %x", stream_id);
    zassert_equal(sys_get_be16(&block[3]),
                  POUCH_CONTENT_TYPE_OCTET_STREAM,
                  "Unexpected content type");
    zassert_equal(block[5], strlen("test/path"), "Unexpected path length");
    zassert_mem_equal(&block[6], "test/path", strlen("test/path"), "Unexpected path");

    zassert_mem_equal(&block[6 + strlen("test/path")], data, sizeof(data), "Unexpected data");
}

ZTEST(uplink, test_stream_multiblock)
{
    transport_session_start();

    struct pouch_stream *stream =
        pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream, "Failed to open stream");

    // write more data than a single block can hold:
    uint8_t data[CONFIG_POUCH_BLOCK_SIZE + 50];
    for (int i = 0; i < sizeof(data); i++)
    {
        data[i] = i & 0xff;  // dummy data
    }

    size_t written = pouch_stream_write(stream, (void *) data, sizeof(data), K_NO_WAIT);
    zassert_equal(written, sizeof(data), "Unexpected write length %d", written);

    zassert_ok(pouch_stream_close(stream, K_NO_WAIT));

    zassert_ok(pouch_uplink_close(K_FOREVER));

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t buf[CONFIG_POUCH_BLOCK_SIZE + 200];
    size_t len = sizeof(buf);
    enum pouch_result result = transport_pull_data(buf, &len);
    zassert_equal(result, POUCH_NO_MORE_DATA, "Unexpected result %d", result);

    uint8_t *blockbuf = skip_pouch_header(buf, &len);
    struct stream_block first_block;
    pull_stream_block(&blockbuf, &first_block);

    zassert_between_inclusive(first_block.block.data_len,
                              1,
                              CONFIG_POUCH_BLOCK_SIZE,
                              "Unexpected block length %d",
                              first_block.block.data_len);

    // 0x01 is the first stream ID for an open stream:
    zassert_between_inclusive(first_block.block.id,
                              0x01,
                              0x7f,
                              "Unexpected stream ID %#x",
                              first_block.block.id);
    zassert_true(first_block.block.first, "Expected first data");
    zassert_false(first_block.block.last, "Unexpected last data");
    zassert_equal(first_block.content_type,
                  POUCH_CONTENT_TYPE_OCTET_STREAM,
                  "Unexpected content type");
    size_t path_len = strlen("test/path");
    zassert_equal(first_block.path_len, path_len, "Unexpected path length");
    zassert_mem_equal(first_block.path, "test/path", path_len, "Unexpected path");
    zassert_mem_equal(first_block.data, data, first_block.data_len, "Unexpected data");

    // next block:
    struct block second_block;
    pull_block(&blockbuf, &second_block);

    zassert_between_inclusive(second_block.data_len,
                              1,
                              sizeof(buf) - first_block.data_len - 3,
                              "Unexpected block length %d",
                              second_block.data_len);

    // 0x01 is the first stream ID for an open stream:
    zassert_between_inclusive(second_block.id,
                              0x01,
                              0x7f,
                              "Unexpected stream ID %#x",
                              second_block.id);
    zassert_equal(second_block.id, first_block.block.id, "Unexpected stream ID");
    zassert_false(second_block.first, "Unexpected first data");
    zassert_true(second_block.last, "Expected last data");
    zassert_mem_equal(second_block.data,
                      &data[first_block.data_len],
                      second_block.data_len,
                      "Unexpected data");
}

ZTEST(uplink, test_stream_multi_stream)
{
    transport_session_start();

    struct pouch_stream *stream1 =
        pouch_uplink_stream_open("test/path1", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream1, "Failed to open stream");

    struct pouch_stream *stream2 =
        pouch_uplink_stream_open("test/path2", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream2, "Failed to open stream");

    const uint8_t data1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    size_t written = pouch_stream_write(stream1, data1, sizeof(data1), K_NO_WAIT);
    zassert_equal(written, sizeof(data1), "Unexpected write length %d", written);

    const uint8_t data2[] = {0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    written = pouch_stream_write(stream2, data2, sizeof(data2), K_NO_WAIT);
    zassert_equal(written, sizeof(data2), "Unexpected write length %d", written);

    zassert_ok(pouch_stream_close(stream1, K_NO_WAIT));
    zassert_ok(pouch_stream_close(stream2, K_NO_WAIT));

    // let processing run:
    k_sleep(K_MSEC(1));
    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);

    buf = skip_pouch_header(buf, &len);
    struct stream_block blocks[2];
    pull_stream_block(&buf, &blocks[0]);

    zassert_true(blocks[0].block.first, "Expected first data");
    zassert_true(blocks[0].block.last, "Expected last data");
    zassert_not_equal(blocks[0].block.id, 0, "Unexpected stream ID");
    zassert_equal(blocks[0].data_len,
                  sizeof(data1),
                  "Unexpected data length %d",
                  blocks[0].data_len);
    zassert_mem_equal(blocks[0].data, data1, sizeof(data1), "Unexpected data");
    zassert_equal(blocks[0].path_len, strlen("test/path1"), "Unexpected path length");
    zassert_mem_equal(blocks[0].path, "test/path1", strlen("test/path1"), "Unexpected path");

    pull_stream_block(&buf, &blocks[1]);

    zassert_true(blocks[1].block.first, "Expected first data");
    zassert_true(blocks[1].block.last, "Expected last data");
    zassert_not_equal(blocks[1].block.id, 0, "Unexpected stream ID");
    zassert_equal(blocks[1].data_len,
                  sizeof(data2),
                  "Unexpected data length %d",
                  blocks[1].data_len);
    zassert_mem_equal(blocks[1].data, data2, sizeof(data2), "Unexpected data");
    zassert_equal(blocks[1].path_len, strlen("test/path2"), "Unexpected path length");
    zassert_mem_equal(blocks[1].path, "test/path2", strlen("test/path2"), "Unexpected path");
}

ZTEST(uplink, test_stream_multi_block_multi_stream)
{
    transport_session_start();

    struct pouch_stream *stream1 =
        pouch_uplink_stream_open("test/path1", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream1, "Failed to open stream");

    struct pouch_stream *stream2 =
        pouch_uplink_stream_open("test/path2", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream2, "Failed to open stream");

    // write more data than a single block can hold:
    uint8_t data[CONFIG_POUCH_BLOCK_SIZE * 2];
    for (int i = 0; i < sizeof(data); i++)
    {
        data[i] = i & 0xff;  // dummy data
    }

    // write the data in chunks to both streams:
    size_t chunk_len = 8;
    for (int i = 0; i < sizeof(data); i += chunk_len)
    {
        size_t written = pouch_stream_write(stream1, &data[i], chunk_len, K_NO_WAIT);
        zassert_equal(written, chunk_len, "Unexpected write length %d", written);

        written = pouch_stream_write(stream2, &data[i], chunk_len, K_NO_WAIT);
        zassert_equal(written, chunk_len, "Unexpected write length %d", written);
    }

    zassert_ok(pouch_stream_close(stream1, K_NO_WAIT));
    zassert_ok(pouch_stream_close(stream2, K_NO_WAIT));

    pouch_uplink_close(K_NO_WAIT);

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t buf[CONFIG_POUCH_BLOCK_SIZE * 5];
    size_t len = sizeof(buf);
    enum pouch_result result = transport_pull_data(buf, &len);
    zassert_equal(result, POUCH_NO_MORE_DATA, "Unexpected result %d", result);

    uint8_t *blockbuf = skip_pouch_header(buf, &len);

    struct
    {
        uint8_t id;
        size_t data_len;
        uint8_t data[CONFIG_POUCH_BLOCK_SIZE * 4];
    } streams[2];

    // pull the first two blocks, which includes path and content type:
    for (int i = 0; i < 2; i++)
    {
        struct stream_block block;
        pull_stream_block(&blockbuf, &block);
        streams[i].id = block.block.id;
        streams[i].data_len = block.data_len;
        char expected_path[12];
        sprintf(expected_path, "test/path%d", i + 1);
        zassert_mem_equal(block.path, expected_path, strlen(expected_path));
        zassert_equal(block.content_type, POUCH_CONTENT_TYPE_OCTET_STREAM);
        memcpy(streams[i].data, block.data, block.data_len);
    }

    // should have pushed the blocks in alternating order:
    zassert_not_equal(streams[0].id, streams[1].id, "Unexpected stream ID");

    for (int i = 0; i < 4; i++)
    {
        struct block block;
        pull_block(&blockbuf, &block);

        zassert_equal(block.id, streams[i % 2].id, "Unexpected stream ID");

        zassert_true(streams[i % 2].data_len + block.data_len <= CONFIG_POUCH_BLOCK_SIZE * 2,
                     "Unexpected total data length %d",
                     streams[i % 2].data_len + block.data_len);
        memcpy(&streams[i % 2].data[streams[i % 2].data_len], block.data, block.data_len);

        streams[i % 2].data_len += block.data_len;

        bool last = !(streams[i % 2].data_len < CONFIG_POUCH_BLOCK_SIZE * 2);
        zassert_equal(block.last, last, "Expected more data");
    }

    for (int i = 0; i < 2; i++)
    {
        zassert_equal(streams[i].data_len,
                      CONFIG_POUCH_BLOCK_SIZE * 2,
                      "stream %d: Unexpected data length %d",
                      i,
                      streams[i].data_len);
        zassert_mem_equal(streams[i].data, data, CONFIG_POUCH_BLOCK_SIZE * 2);
    }
}


ZTEST(uplink, test_stream_max_count)
{
    transport_session_start();

    struct pouch_stream *streams[POUCH_STREAMS_MAX];
    for (int i = 0; i < POUCH_STREAMS_MAX; i++)
    {
        streams[i] = pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
        zassert_not_null(streams[i], "Failed to open stream");
    }

    struct pouch_stream *stream =
        pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_is_null(stream, "Expected to fail to open stream");

    for (int i = 0; i < POUCH_STREAMS_MAX; i++)
    {
        zassert_ok(pouch_stream_close(streams[i], K_NO_WAIT));
    }
}

ZTEST(uplink, test_stream_empty)
{
    transport_session_start();

    struct pouch_stream *stream =
        pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream, "Failed to open stream");

    zassert_ok(pouch_stream_close(stream, K_NO_WAIT));

    pouch_uplink_close(K_FOREVER);

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_equal(len, 0, "Unexpected block");
}

ZTEST(uplink, test_stream_fail_to_close_stream)
{
    struct pouch_stream *stream =
        pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream, "Failed to open stream");

    zassert_true(pouch_stream_is_valid(stream), "Expected stream to be valid");

    transport_session_start();

    zassert_true(pouch_stream_is_valid(stream), "Expected stream to be valid");

    uint8_t data1[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    size_t written = pouch_stream_write(stream, data1, sizeof(data1), K_NO_WAIT);
    zassert_equal(written, sizeof(data1), "Unexpected write length %d", written);

    // close the pouch, but the stream is still open:
    zassert_ok(pouch_uplink_close(K_FOREVER));

    transport_session_end();

    // the stream should no longer be valid:
    zassert_false(pouch_stream_is_valid(stream), "Expected stream to be invalid");

    // closing the stream should succeed, but data should not be sent:
    zassert_ok(pouch_stream_close(stream, K_NO_WAIT));

    transport_session_start();

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t *buf;
    size_t len = read_data(&buf, CONFIG_POUCH_BLOCK_SIZE);
    zassert_equal(len, 0, "Unexpected block");
}

ZTEST(uplink, test_stream_length_aligned_to_block_size)
{
    transport_session_start();

    struct pouch_stream *stream =
        pouch_uplink_stream_open("test/path", POUCH_CONTENT_TYPE_OCTET_STREAM);
    zassert_not_null(stream, "Failed to open stream");

    // Write data that is exactly aligned to the size of two blocks. Need to account for the size
    // of the content_type, path_length and path in the first block.
    size_t data_len = (CONFIG_POUCH_BLOCK_SIZE) * 2 - (2 + 1 + strlen("test/path"));
    uint8_t *data = malloc(data_len);
    for (int i = 0; i < sizeof(data); i++)
    {
        data[i] = i & 0xff;  // dummy data
    }

    size_t written = pouch_stream_write(stream, (void *) data, data_len, K_NO_WAIT);
    zassert_equal(written, data_len, "Unexpected write length %d", written);

    zassert_ok(pouch_stream_close(stream, K_NO_WAIT));

    zassert_ok(pouch_uplink_close(K_FOREVER));

    // let processing run:
    k_sleep(K_MSEC(1));

    uint8_t buf[2 * CONFIG_POUCH_BLOCK_SIZE + 100];
    size_t len = sizeof(buf);
    enum pouch_result result = transport_pull_data(buf, &len);
    zassert_equal(result, POUCH_NO_MORE_DATA, "Unexpected result %d", result);

    uint8_t *start_of_blocks = skip_pouch_header(buf, &len);
    uint8_t *blockbuf = start_of_blocks;

    struct stream_block block1;
    pull_stream_block(&blockbuf, &block1);

    zassert_true(block1.block.first, "Expected first data");
    zassert_false(block1.block.last, "Unexpected last data");
    zassert_not_equal(block1.block.id, 0, "Unexpected stream ID");
    zassert_equal(block1.block.data_len,
                  CONFIG_POUCH_BLOCK_SIZE,
                  "Unexpected block length %d",
                  block1.data_len);
    zassert_mem_equal(block1.data, data, block1.data_len);

    struct block block2;
    pull_block(&blockbuf, &block2);

    zassert_false(block2.first, "Unexpected first data");
    zassert_true(block2.last, "Expected last data");
    zassert_equal(block2.id, block1.block.id, "Unexpected stream ID");
    zassert_equal(block2.data_len,
                  CONFIG_POUCH_BLOCK_SIZE,
                  "Unexpected block length %d",
                  block2.data_len);
    zassert_mem_equal(block2.data, &data[block1.data_len], block2.data_len);

    // Should include entire data length:
    zassert_equal(block1.data_len + block2.data_len,
                  data_len,
                  "Unexpected data length %d, expected %d",
                  block1.data_len + block2.data_len,
                  data_len);

    // should have consumed all the data:
    zassert_equal(blockbuf - start_of_blocks,
                  len,
                  "Unexpected buf length %d, expected %d",
                  blockbuf - start_of_blocks,
                  len);
}
