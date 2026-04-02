# Proposed Port API

## API

The proposed Port API targets the core Pouch functionality. It is meant
to be used internally to ensure that the repository works on multiple
platforms, and will not be exposed publicly for users of Pouch.

Opaque pointers have been avoided throughout so that the API functions
don't allocate memory (statically or dynamically). The caller controls
its own memory when instantiating typedefs.

This proposal considers the following as required Pouch dependencies:

- PSA (from platform)
- mbedTLS (from platform)
- ZCBOR (library)

### Atomic

`port/include/pouch/port.h`

```c
typedef port_atomic_t <type>;

port_atomic_t port_atomic_dec(port_atomic_t *target);

port_atomic_t port_atomic_inc(port_atomic_t *target);

port_atomic_t port_atomic_get(port_atomic_t *target);

port_atomic_t port_atomic_set(port_atomic_t *target, port_atomic_t value);

bool port_atomic_test_bit(const port_atomic_t *target, int bit);

bool port_atomic_test_and_clear_bit(port_atomic_t *target, int bit);

bool port_atomic_test_and_set_bit(port_atomic_t *target, int bit);
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

We need to  replace ATOMIC_DEFINE and ATOMIC_INIT using the startup hook
API, or some other initialization approach.

- uplink.c
  - `struct ouch_uplink`: atomic_t
  - `session_is_active()`: atomic_test_bit
  - `pouch_is_open()`: atomic_test_bit
  - `pouch_is_closing()`: atomic_test_bit
  - `process_blocks()`: atomic_set_bit
  - `end_session()`: atomic_clear_bit
  - `pouch_uplink_close()`: atomic_test_and_set_bit
  - `uplink_session_id()`: atomic_get
  - `pouch_uplink_start()`: atomic_test_and_set_bit, atomic_clear_bit
  - `pouch_uplink_finish()`: atomic_inc, atomic_clear
- buf.c
  - `atomic_t`
  - `buf_alloc()`: atomic_inc
  - `buf_free()`: atomic_dec
  - `buf_active_count()`: atomic_get
- stream.c
  - atomic_t
  - `new_stream_id()`: atomic_inc
  - `pouch_uplink_stream_open()`: atomic_inc, atomic_dec
  - `pouch_stream_close()`: atomic_dec
- saead/downlink.c
  - `is_valid_downlink()`: atomic_test_bit
  - `saead_downlink_session_start()`: atomic_set_bit
  - `saead_downlink_pouch_start()`: atomic_test_bit
  - `saead_downlink_block_decrypt()`: atomic_set_bit
- saead/session.c
  - `session_end()`: atomic_test_and_clear_bit
  - `session_pouch_start()`: atomic_test_bit
  - `session_decrypt_block()`: atomic_set_bit
- saead/uplink.c
  - `saead_uplink_session_start()`: ATOMIC_INIT, atomic_set_bit
  - `saead_uplink_encrypt_block()`: atomic_test_bit
  - `saead_uplink_session_matches()`: atomic_test_bit
  - `saead_uplink_session_key_copy()`: atomic_test_bit
- lib/pouch_gatt_common/sender.c
  - `struct pouch_gatt_sender`: ATOMIC_DEFINE, atomic_t
  - `calculate_outstanding_packets()`: atomic_get
  - `calculate_usable_window()`: atomic_get
  - `send_data()`: atomic_test_and_set_bit, atomic_get, atomic_set,
    atomic_clear_bit
  - `pouch_gatt_sender_receive_ack()`: atomic_set, atomic_test_bit,
    atomic_get
  - `pouch_gatt_sender_create()`: atomic_set, atomic_clear

</details>

#### Remaining Unknowns

Zephyr uses long but FreeRTOS uses uint32_t for type. Does this cause
issues for port_atomic_dec() which could be <0 for Zephyr?

### Mutex

`port/include/pouch/port.h`

```c
typedef void port_mutex_t <struct>;

void port_mutex_init(port_mutex_t *mutex);

/* int32_t replaces k_timeout_t params */
bool port_mutex_lock(port_mutex_t *mutex, int32_t ms_to_wait);

bool port_mutex_unlock(port_mutex_t *mutex);
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

We need to replace K_MUTEX_DEFINE using the startup hook API, or some
other initialization approach.

- entry.c
  - `K_MUTEX_DEFINE`
  - `pouch_uplink_entry_write()`: k_mutex_lock/unlock, k_timeout_t as
    param
  - `entry_block_close()`: k_mutex_lock/unlock, k_timeout_t as param

Note: mutexes are likely needed in more places than currently used.

</details>

### Message Queues


`port/include/pouch/port.h`

```c
typedef port_msgq_t <struct>;

/* The caller allocates the buffer memory used by the queue. */
int port_msgq_init(port_msgq_t *msgq,
                   uint8_t *msgq_buffer,
                   size_t msg_size);

int port_msgq_send(port_msgq_t *msgq,
                   const void *data,
                   int32 timeout);

int port_msgq_rcv(port_msq_t *msgq
                  void *buf,
                  int32 timeout);
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

We need to replace K_MSGQ_DEFINE using the startup hook API, or some
other initialization approach:

- pouch.c
  - `K_MSGQ_DEFINE`
  - `dispatch_events()`: k_msgq_get()
  - `pouch_event_emit()`: k_msgq_put()

</details>

### Work Queues

`port/include/pouch/port.h`

```c
typedef port_work_q_t <struct>;

typedef port_work_t <struct>;

/* This callback doesn't pass work as param. We're not currently using
 * that param and this simplies the port work.
 */
typedef void (*port_work_handler_t)(void);

void port_work_init(port_work_t *work, port_work_handler_t handler);

int port_work_submit_to_queue(port_work_q_t *queue, port_work_t *work);
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

Downlink and Uplink use the system work queue (`k_work_submit()`) and
will need to be updated to work with this approach.

- pouch.c
  - `dispatch_events()`: takes k_work as param
  - `pouch_event_emit()`: k_work_submit_to_queue()
- uplink.c
  - `struct ouch_uplink`: k_work
  - `uplink_enqueue()`: k_work_submit
  - `pouch_uplink_close()`: k_work_submit
  - `uplink_init()`: k_work_init
  - `pouch_uplink_start()`: k_work_submit
- downlink.c
  - `struct decrypt`: k_work
  - `struct consume`: k_work_q, k_work
  - `downlink_init()`: k_work_init, k_work_q as param
  - `consume_blocks()`: k_work_submit_to_queue, k_work as param
  - `decrypt_blocks()`: k_work_submit_to_queue, k_work as param
  - `block_downlink_push()`: k_work_submit

</details>

### Linked List

`port/include/pouch/port.h`

```c
typedef port_slist_t <struct>;

typedef port_slist_node_t <struct>;

void port_slist_init(port_slist_t *list);

void port_slist_append(port_slist_t *list, port_slist_node_t *node);

port_slist_node_t *port_slist_get(port_slist_t *list);

/* CONTAINER_OF is a simple Zephyr macro that ports can define */
PORT_CONTAINER_OF
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

- buf.c
  - `struct pouch_buf`: sys_snode_t
  - `buf_queue_init()`: sys_slist_init
  - `buf_queue_submit()`: sys_slist_append
  - `buf_queue_get()`: sys_snode_t, sys_slist_get, CONTAINER_OF
  - `buf_queue_peek()`: sys_snode_t, sys_slist_get, CONTAINER_OF

</details>

### Big Endian

`port/include/pouch/port.h`

```c
port_get_be16(const uint8_t src[2])

port_get_be32(const uint8_t src[4]) /* not currently used */

port_get_be64(const uint8_t src[8]) /* not currently used */

port_put_be16(const uint8_t src[2])
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

- buf.c
  - `pouch_bufview_read_be16()`: sys_get_be16
  - `pouch_bufview_read_be32()`: sys_get_be32
  - `pouch_bufview_read_be64()`: sys_get_be64
- entry.c
  - `write_entry()`: sys_put_be16()
- block.c
  - `update_block_header()`: sys_put_be16
  - `write_block_header()`: sys_put_be16
  - `block_size_write()`: sys_put_be16
- stream.c
  - `write_stream_header()`: sys_put_be16
- saead/session.c
  - `nonce_generate()`: sys_put_be16

</details>

### Startup Hook

`port/include/pouch/port.h`

```c
/* Register callback to run at application startup
 *   - callback format: void(*)(void);
 */
PORT_REGISTER_STARTUP_HOOK(my_startup_hook);
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

- pouch.c
  - `pouch_module_init()`: SYS_INIT to set up queues

</details>

Note: the startup hook is likely needed to replace several of the
compile-time macros currently used in Zephyr where runtime
initialization is needed for other platforms.

### Misc

`port/include/pouch/port.h`

```c
#define PORT_MIN

#define PORT_DIV_ROUND_UP

#define PORT_STATIC_ASSERT
```

We can likely replace this function with the mbedtls version:

```
base64_encode();
```

#### This API Replaces

<details>

<summary>Click to unfold list</summary>

- buf.c
  - `buf_trim_start()`: MIN
  - `buf_trim_end()`: MIN
- saead/session.c
  - DIV_ROUND_UP
  - `session_key_info_build()`: base64_encode
- lib/pouch_gatt_common/packetizer.c
  - BUILD_ASSERT
- lib/pouch_gatt_common/receiver.c
  - BUILD_ASSERT

</details>

## Likely provided by the platform

```c
malloc();

free();
```

<details>

<summary>Click to unfold list</summary>

- buf.c
  - `buf_alloc()`: malloc
  - `buf_free()`: free

</details>

## Iterable Sections

One big unknown with the porting work centers around iterable sections,
which is a Zephyr feature. It's possible this can be left intact in the
tree by implementing this in ESP-IDF using [Linker
Fragments](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/linker-script-generation.html#creating-and-specifying-a-linker-fragment-file),
but more exploration is needed to scope this work.

#### Calls that use Iterable Sections

<details>

<summary>Click to unfold list</summary>

- entry.c
  - `downlink_start()`: STRUCT_SECTION_FOREACH
  - `downlink_data()`: STRUCT_SECTION_FOREACH
- golioth_sdk/dispatch.c
  - POUCH_DOWNLINK_HANDLER
  - `pouch_downlink_data()`: STRUCT_SECTION_FOREACH
  - `golioth_sync_to_cloud()`: STRUCT_SECTION_FOREACH
- golioth_sdk/ota.c
    - GOLIOTH_DOWNLINK_HANDLER
    - GOLIOTH_UPLINK_HANDLER
- golioth_sdk/ota_upper.c
  - `golioth_ota_set_status()`: STRUCT_SECTION_FOREACH
  - `golioth_ota_manifest_receive_one()`: STRUCT_SECTION_FOREACH
  - `golioth_ota_manifest_complete()`: STRUCT_SECTION_COUNT,
    STRUCT_SECTION_GET, STRUCT_SECTION_FOREACH
  - `golioth_ota_receive_component()`: STRUCT_SECTION_FOREACH
  - `golioth_ota_get_status()`: STRUCT_SECTION_COUNT,
    STRUCT_SECTION_GET
- golioth_sdk/settings.c
  - GOLIOTH_DOWNLINK_HANDLER
  - GOLIOTH_UPLINK_HANDLER
- golioth_sdk/settings_callbacks.c
  - `golioth_settings_receive_one()`: STRUCT_SECTION_FOREACH

</details>

## Logging

We will follow [the pattern established in the Golioth Firmware SDK
](https://github.com/golioth/golioth-firmware-sdk/blob/faa07040089ea055cb91386e183ae2c61c860ec5/include/golioth/golioth_sys.h#L195-L306)to
implement portable logging with a `PORT_LOG*(TAG, ...)` format.
