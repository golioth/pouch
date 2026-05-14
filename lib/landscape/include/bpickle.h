/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * bpickle.h - C implementation of the bpickle binary serialization format.
 *
 * Copyright (c) 2006, Gustavo Niemeyer <gustavo@niemeyer.net>
 * C implementation by Trond Snekvik / Canonical Ltd.
 *
 * Wire-compatible with landscape/lib/bpickle.py.
 *
 * Type prefixes:
 *   b  bool
 *   i  int (signed 64-bit)
 *   f  float (double)
 *   s  bytes
 *   u  unicode string (UTF-8)
 *   l  list
 *   t  tuple  (decoded as list)
 *   d  dict
 *   n  None
 *
 * Usage example (encoding a {"messages": [...]} payload):
 *
 *   struct bpickle_val *msg = bpickle_dict_new();
 *   bpickle_dict_set_str(msg, "type",    bpickle_str("test-msg"));
 *   bpickle_dict_set_str(msg, "content", bpickle_str("hello"));
 *
 *   struct bpickle_val *msgs = bpickle_list_new();
 *   bpickle_list_append(msgs, msg);
 *
 *   struct bpickle_val *payload = bpickle_dict_new();
 *   bpickle_dict_set_str(payload, "messages", msgs);
 *
 *   struct bpickle_buf buf = {0};
 *   int rc = bpickle_dumps(payload, &buf);
 *   // use buf.data / buf.len ...
 *   bpickle_buf_free(&buf);
 *   bpickle_val_free(payload);
 *
 * Decoding:
 *
 *   struct bpickle_val *root = NULL;
 *   int rc = bpickle_loads(data, datalen, &root);
 *   if (rc == 0) {
 *       struct bpickle_val *messages = bpickle_dict_get_str(root, "messages");
 *       for (size_t i = 0; i < bpickle_list_len(messages); i++) {
 *           struct bpickle_val *m = bpickle_list_get(messages, i);
 *           const char *type = bpickle_val_str(bpickle_dict_get_str(m, "type"));
 *           // ...
 *       }
 *       bpickle_val_free(root);
 *   }
 */

#ifndef BPICKLE_H
#define BPICKLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Value types
 * ---------------------------------------------------------------------- */

enum bpickle_type {
	BPICKLE_NONE,
	BPICKLE_BOOL,
	BPICKLE_INT,
	BPICKLE_FLOAT,
	BPICKLE_BYTES,   /* raw bytes ('s' on wire) */
	BPICKLE_STR,     /* unicode string ('u' on wire) */
	BPICKLE_LIST,    /* list or tuple ('l'/'t' on wire) */
	BPICKLE_DICT,    /* dict ('d' on wire) */
};

/**
 * A single bpickle value.  Treat as opaque; use the accessor functions below.
 */
struct bpickle_val;

/* -------------------------------------------------------------------------
 * Output buffer
 * ---------------------------------------------------------------------- */

/** Growable byte buffer used by bpickle_dumps(). */
struct bpickle_buf {
	uint8_t *data;
	size_t   len;
	size_t   cap;
};

/** Release memory owned by buf.  buf itself is not freed. */
void bpickle_buf_free(struct bpickle_buf *buf);

/* -------------------------------------------------------------------------
 * Constructor helpers
 * ---------------------------------------------------------------------- */

/** Create a None value. */
struct bpickle_val *bpickle_none(void);

/** Create a bool value. */
struct bpickle_val *bpickle_bool(bool v);

/** Create an integer value (signed 64-bit). */
struct bpickle_val *bpickle_int(int64_t v);

/** Create a float (double) value. */
struct bpickle_val *bpickle_float(double v);

/**
 * Create a bytes value from a buffer.  The data is copied.
 * @param data  raw bytes
 * @param len   number of bytes
 */
struct bpickle_val *bpickle_bytes(const void *data, size_t len);

/**
 * Create a unicode string value from a NUL-terminated UTF-8 string.
 * The string is copied.
 */
struct bpickle_val *bpickle_str(const char *s);

/**
 * Create a unicode string value from a length-delimited UTF-8 buffer.
 * The string is copied.
 */
struct bpickle_val *bpickle_str_n(const char *s, size_t len);

/** Create an empty list. */
struct bpickle_val *bpickle_list_new(void);

/** Create an empty dict. */
struct bpickle_val *bpickle_dict_new(void);

/* -------------------------------------------------------------------------
 * List operations
 * ---------------------------------------------------------------------- */

/**
 * Append a value to a list.
 * The list takes ownership of @p val.
 * @return 0 on success, -1 on failure (e.g. wrong type or allocation failure).
 */
int bpickle_list_append(struct bpickle_val *list, struct bpickle_val *val);

/** Return the number of elements in a list, or 0 if not a list. */
size_t bpickle_list_len(const struct bpickle_val *list);

/**
 * Return the element at index @p i (zero-based), or NULL if out of range.
 * The list retains ownership.
 */
struct bpickle_val *bpickle_list_get(const struct bpickle_val *list, size_t i);

/* -------------------------------------------------------------------------
 * Dict operations
 * ---------------------------------------------------------------------- */

/**
 * Set a key/value pair in a dict.  Both key and value are owned by the dict
 * after this call (do not free them yourself).  If a key with the same
 * string already exists its old value is freed and replaced.
 *
 * Keys must be either BPICKLE_STR or BPICKLE_BYTES, mirroring the Python
 * wire format where dict keys are always strings or byte strings.
 *
 * @return 0 on success, -1 on failure.
 */
int bpickle_dict_set(struct bpickle_val *dict,
		     struct bpickle_val *key,
		     struct bpickle_val *val);

/**
 * Convenience wrapper: set a string-keyed entry.
 * Equivalent to bpickle_dict_set(dict, bpickle_str(key), val).
 */
int bpickle_dict_set_str(struct bpickle_val *dict,
			 const char *key,
			 struct bpickle_val *val);

/**
 * Look up a value by string key.
 * @return the value (dict retains ownership), or NULL if not found.
 */
struct bpickle_val *bpickle_dict_get_str(const struct bpickle_val *dict,
					 const char *key);

/** Return the number of entries in a dict, or 0 if not a dict. */
size_t bpickle_dict_len(const struct bpickle_val *dict);

/* -------------------------------------------------------------------------
 * Value accessors
 * ---------------------------------------------------------------------- */

/** Return the type tag of a value, or BPICKLE_NONE if val is NULL. */
enum bpickle_type bpickle_val_type(const struct bpickle_val *val);

/** Return the boolean content of a BPICKLE_BOOL value (false if wrong type). */
bool bpickle_val_bool(const struct bpickle_val *val);

/** Return the integer content of a BPICKLE_INT value (0 if wrong type). */
int64_t bpickle_val_int(const struct bpickle_val *val);

/** Return the float content of a BPICKLE_FLOAT value (0.0 if wrong type). */
double bpickle_val_float(const struct bpickle_val *val);

/**
 * Return the NUL-terminated string content of a BPICKLE_STR value,
 * or NULL if wrong type.  The pointer is valid as long as @p val lives.
 */
const char *bpickle_val_str(const struct bpickle_val *val);

/**
 * Return a pointer to the raw bytes of a BPICKLE_BYTES value,
 * or NULL if wrong type.  The pointer is valid as long as @p val lives.
 */
const uint8_t *bpickle_val_bytes(const struct bpickle_val *val);

/**
 * Return the byte count of a BPICKLE_BYTES or BPICKLE_STR value,
 * or 0 if wrong type.
 */
size_t bpickle_val_bytes_len(const struct bpickle_val *val);

/* -------------------------------------------------------------------------
 * Serialization / deserialization
 * ---------------------------------------------------------------------- */

/**
 * Serialize @p val into @p buf.
 * @p buf must be zero-initialized on first use (or after bpickle_buf_free).
 * On success, buf->data holds the encoded bytes and buf->len their count.
 * Caller must eventually call bpickle_buf_free().
 *
 * @return 0 on success, -1 on error (unsupported type or allocation failure).
 */
int bpickle_dumps(const struct bpickle_val *val, struct bpickle_buf *buf);

/**
 * Deserialize @p len bytes from @p data into a new bpickle_val tree.
 * On success, *out is set to the root value (caller owns it, must call
 * bpickle_val_free()).
 *
 * @return 0 on success, -1 on parse error.
 */
int bpickle_loads(const uint8_t *data, size_t len, struct bpickle_val **out);

/* -------------------------------------------------------------------------
 * Memory management
 * ---------------------------------------------------------------------- */

/**
 * Recursively free a value tree.
 * Safe to call with NULL.
 */
void bpickle_val_free(struct bpickle_val *val);

#ifdef __cplusplus
}
#endif

#endif /* BPICKLE_H */
