/*
 * Copyright (c) 2024 Golioth
 */

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct golioth_ble_gatt_packetizer;

enum golioth_ble_gatt_packetizer_result
{
    GOLIOTH_BLE_GATT_PACKETIZER_MORE_DATA,
    GOLIOTH_BLE_GATT_PACKETIZER_NO_MORE_DATA,
    GOLIOTH_BLE_GATT_PACKETIZER_EMPTY_PAYLOAD,
    GOLIOTH_BLE_GATT_PACKETIZER_ERROR,
};

typedef enum golioth_ble_gatt_packetizer_result (
    *golioth_ble_gatt_packetizer_fill_cb)(void *dst, size_t *dst_len, void *user_arg);

struct golioth_ble_gatt_packetizer *golioth_ble_gatt_packetizer_start_buffer(const void *src,
                                                                             size_t src_len);
struct golioth_ble_gatt_packetizer *golioth_ble_gatt_packetizer_start_callback(
    golioth_ble_gatt_packetizer_fill_cb cb,
    void *user_arg);
enum golioth_ble_gatt_packetizer_result golioth_ble_gatt_packetizer_get(
    struct golioth_ble_gatt_packetizer *packetizer,
    void *dst,
    size_t *dst_len);
int golioth_ble_gatt_packetizer_error(struct golioth_ble_gatt_packetizer *packetizer);
void golioth_ble_gatt_packetizer_finish(struct golioth_ble_gatt_packetizer *packetizer);

ssize_t golioth_ble_gatt_packetizer_decode(const void *buf,
                                           size_t buf_len,
                                           const void **payload,
                                           bool *is_first,
                                           bool *is_last);
