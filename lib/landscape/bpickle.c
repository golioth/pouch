/*
 * Copyright (c) 2026 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * bpickle.c - C implementation of the bpickle binary serialization format.
 *
 * Wire-compatible with landscape/lib/bpickle.py.
 *
 * Type prefixes (same as Python implementation):
 *   b  bool
 *   i  int (signed 64-bit)
 *   f  float (double)
 *   s  bytes
 *   u  unicode string (UTF-8)
 *   l  list
 *   t  tuple  (encoded/decoded the same as list)
 *   d  dict   (keys sorted lexicographically on encode)
 *   n  None
 */

#include "bpickle.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal data structures
 * ====================================================================== */

struct bpickle_kv
{
    struct bpickle_val *key;
    struct bpickle_val *val;
};

struct bpickle_val
{
    enum bpickle_type type;
    union
    {
        bool b;
        int64_t i;
        double f;

        /* BYTES and STR share the same layout: data + len */
        struct
        {
            uint8_t *data; /* NUL-terminated for STR */
            size_t len;    /* byte length (excl. NUL for STR) */
        } s;

        /* LIST (and TUPLE decoded as LIST) */
        struct
        {
            struct bpickle_val **items;
            size_t len;
            size_t cap;
        } list;

        /* DICT */
        struct
        {
            struct bpickle_kv *pairs;
            size_t len;
            size_t cap;
        } dict;
    } u;
};

/* =========================================================================
 * Constructors
 * ====================================================================== */

struct bpickle_val *bpickle_none(void)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (v)
        v->type = BPICKLE_NONE;
    return v;
}

struct bpickle_val *bpickle_bool(bool b)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (v)
    {
        v->type = BPICKLE_BOOL;
        v->u.b = b;
    }
    return v;
}

struct bpickle_val *bpickle_int(int64_t i)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (v)
    {
        v->type = BPICKLE_INT;
        v->u.i = i;
    }
    return v;
}

struct bpickle_val *bpickle_float(double f)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (v)
    {
        v->type = BPICKLE_FLOAT;
        v->u.f = f;
    }
    return v;
}

struct bpickle_val *bpickle_bytes(const void *data, size_t len)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->type = BPICKLE_BYTES;
    v->u.s.len = len;
    v->u.s.data = malloc(len + 1);
    if (!v->u.s.data)
    {
        free(v);
        return NULL;
    }
    memcpy(v->u.s.data, data, len);
    v->u.s.data[len] = 0;
    return v;
}

struct bpickle_val *bpickle_str_n(const char *s, size_t len)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->type = BPICKLE_STR;
    v->u.s.len = len;
    v->u.s.data = malloc(len + 1);
    if (!v->u.s.data)
    {
        free(v);
        return NULL;
    }
    memcpy(v->u.s.data, s, len);
    v->u.s.data[len] = 0;
    return v;
}

struct bpickle_val *bpickle_str(const char *s)
{
    return bpickle_str_n(s, strlen(s));
}

struct bpickle_val *bpickle_list_new(void)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (v)
        v->type = BPICKLE_LIST;
    return v;
}

struct bpickle_val *bpickle_dict_new(void)
{
    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (v)
        v->type = BPICKLE_DICT;
    return v;
}

/* =========================================================================
 * Memory management
 * ====================================================================== */

void bpickle_val_free(struct bpickle_val *v)
{
    if (!v)
        return;

    switch (v->type)
    {
        case BPICKLE_BYTES:
        case BPICKLE_STR:
            free(v->u.s.data);
            break;

        case BPICKLE_LIST:
            for (size_t i = 0; i < v->u.list.len; i++)
                bpickle_val_free(v->u.list.items[i]);
            free(v->u.list.items);
            break;

        case BPICKLE_DICT:
            for (size_t i = 0; i < v->u.dict.len; i++)
            {
                bpickle_val_free(v->u.dict.pairs[i].key);
                bpickle_val_free(v->u.dict.pairs[i].val);
            }
            free(v->u.dict.pairs);
            break;

        default:
            break;
    }
    free(v);
}

void bpickle_buf_free(struct bpickle_buf *buf)
{
    if (!buf)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

/* =========================================================================
 * List operations
 * ====================================================================== */

int bpickle_list_append(struct bpickle_val *list, struct bpickle_val *val)
{
    if (!list || list->type != BPICKLE_LIST || !val)
        return -1;

    if (list->u.list.len == list->u.list.cap)
    {
        size_t newcap = list->u.list.cap ? list->u.list.cap * 2 : 4;
        struct bpickle_val **tmp = realloc(list->u.list.items, newcap * sizeof(*tmp));
        if (!tmp)
            return -1;
        list->u.list.items = tmp;
        list->u.list.cap = newcap;
    }
    list->u.list.items[list->u.list.len++] = val;
    return 0;
}

size_t bpickle_list_len(const struct bpickle_val *list)
{
    if (!list || list->type != BPICKLE_LIST)
        return 0;
    return list->u.list.len;
}

struct bpickle_val *bpickle_list_get(const struct bpickle_val *list, size_t i)
{
    if (!list || list->type != BPICKLE_LIST)
        return NULL;
    if (i >= list->u.list.len)
        return NULL;
    return list->u.list.items[i];
}

/* =========================================================================
 * Dict operations
 * ====================================================================== */

int bpickle_dict_set(struct bpickle_val *dict, struct bpickle_val *key, struct bpickle_val *val)
{
    if (!dict || dict->type != BPICKLE_DICT || !key || !val)
        return -1;
    if (key->type != BPICKLE_STR && key->type != BPICKLE_BYTES)
        return -1;

    const char *kstr = (const char *) key->u.s.data;
    size_t klen = key->u.s.len;

    /* Replace existing entry with the same key */
    for (size_t i = 0; i < dict->u.dict.len; i++)
    {
        struct bpickle_val *k = dict->u.dict.pairs[i].key;
        if (k->u.s.len == klen && memcmp(k->u.s.data, kstr, klen) == 0)
        {
            bpickle_val_free(k);
            bpickle_val_free(dict->u.dict.pairs[i].val);
            dict->u.dict.pairs[i].key = key;
            dict->u.dict.pairs[i].val = val;
            return 0;
        }
    }

    /* Grow the pair array if needed */
    if (dict->u.dict.len == dict->u.dict.cap)
    {
        size_t newcap = dict->u.dict.cap ? dict->u.dict.cap * 2 : 4;
        struct bpickle_kv *tmp = realloc(dict->u.dict.pairs, newcap * sizeof(*tmp));
        if (!tmp)
            return -1;
        dict->u.dict.pairs = tmp;
        dict->u.dict.cap = newcap;
    }
    dict->u.dict.pairs[dict->u.dict.len].key = key;
    dict->u.dict.pairs[dict->u.dict.len].val = val;
    dict->u.dict.len++;
    return 0;
}

int bpickle_dict_set_str(struct bpickle_val *dict, const char *key, struct bpickle_val *val)
{
    struct bpickle_val *k = bpickle_str(key);
    if (!k)
        return -1;
    int rc = bpickle_dict_set(dict, k, val);
    if (rc != 0)
        bpickle_val_free(k);
    return rc;
}

struct bpickle_val *bpickle_dict_get_str(const struct bpickle_val *dict, const char *key)
{
    if (!dict || dict->type != BPICKLE_DICT || !key)
        return NULL;
    size_t klen = strlen(key);
    for (size_t i = 0; i < dict->u.dict.len; i++)
    {
        struct bpickle_val *k = dict->u.dict.pairs[i].key;
        if ((k->type == BPICKLE_STR || k->type == BPICKLE_BYTES) && k->u.s.len == klen
            && memcmp(k->u.s.data, key, klen) == 0)
            return dict->u.dict.pairs[i].val;
    }
    return NULL;
}

size_t bpickle_dict_len(const struct bpickle_val *dict)
{
    if (!dict || dict->type != BPICKLE_DICT)
        return 0;
    return dict->u.dict.len;
}

/* =========================================================================
 * Value accessors
 * ====================================================================== */

enum bpickle_type bpickle_val_type(const struct bpickle_val *v)
{
    return v ? v->type : BPICKLE_NONE;
}

bool bpickle_val_bool(const struct bpickle_val *v)
{
    return (v && v->type == BPICKLE_BOOL) ? v->u.b : false;
}

int64_t bpickle_val_int(const struct bpickle_val *v)
{
    return (v && v->type == BPICKLE_INT) ? v->u.i : 0;
}

double bpickle_val_float(const struct bpickle_val *v)
{
    return (v && v->type == BPICKLE_FLOAT) ? v->u.f : 0.0;
}

const char *bpickle_val_str(const struct bpickle_val *v)
{
    if (!v || v->type != BPICKLE_STR)
        return NULL;
    return (const char *) v->u.s.data;
}

const uint8_t *bpickle_val_bytes(const struct bpickle_val *v)
{
    if (!v || v->type != BPICKLE_BYTES)
        return NULL;
    return v->u.s.data;
}

size_t bpickle_val_bytes_len(const struct bpickle_val *v)
{
    if (!v || (v->type != BPICKLE_BYTES && v->type != BPICKLE_STR))
        return 0;
    return v->u.s.len;
}

/* =========================================================================
 * Encoding (dumps)
 * ====================================================================== */

static int buf_append(struct bpickle_buf *buf, const void *data, size_t len)
{
    if (buf->len + len > buf->cap)
    {
        size_t newcap = buf->cap ? buf->cap * 2 : 64;
        while (newcap < buf->len + len)
            newcap *= 2;
        uint8_t *tmp = realloc(buf->data, newcap);
        if (!tmp)
            return -1;
        buf->data = tmp;
        buf->cap = newcap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 0;
}

static int buf_appendf(struct bpickle_buf *buf, const char *fmt, ...)
{
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t) n >= sizeof(tmp))
        return -1;
    return buf_append(buf, tmp, (size_t) n);
}

/* Forward declaration */
static int dumps_val(const struct bpickle_val *v, struct bpickle_buf *buf);

static int dumps_bool(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    char tmp[3] = {'b', v->u.b ? '1' : '0', '\0'};
    return buf_append(buf, tmp, 2);
}

static int dumps_int(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    return buf_appendf(buf, "i%" PRId64 ";", v->u.i);
}

static int dumps_float(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    /*
     * Python repr() for floats uses the shortest round-trip representation.
     * %.17g gives the same guarantee in C.
     */
    return buf_appendf(buf, "f%.17g;", v->u.f);
}

static int dumps_bytes(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    if (buf_appendf(buf, "s%zu:", v->u.s.len) != 0)
        return -1;
    return buf_append(buf, v->u.s.data, v->u.s.len);
}

static int dumps_str(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    if (buf_appendf(buf, "u%zu:", v->u.s.len) != 0)
        return -1;
    return buf_append(buf, v->u.s.data, v->u.s.len);
}

static int dumps_list(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    if (buf_append(buf, "l", 1) != 0)
        return -1;
    for (size_t i = 0; i < v->u.list.len; i++)
    {
        if (dumps_val(v->u.list.items[i], buf) != 0)
            return -1;
    }
    return buf_append(buf, ";", 1);
}

/* Comparison function used to sort dict keys (lexicographic on encoded key) */
static int kv_cmp(const void *a, const void *b)
{
    const struct bpickle_kv *ka = (const struct bpickle_kv *) a;
    const struct bpickle_kv *kb = (const struct bpickle_kv *) b;
    size_t la = ka->key->u.s.len;
    size_t lb = kb->key->u.s.len;
    size_t mn = la < lb ? la : lb;
    int rc = memcmp(ka->key->u.s.data, kb->key->u.s.data, mn);
    if (rc != 0)
        return rc;
    return (la < lb) ? -1 : (la > lb) ? 1 : 0;
}

static int dumps_dict(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    size_t n = v->u.dict.len;

    /* Sort a temporary index array to avoid mutating the dict */
    struct bpickle_kv *sorted = malloc(n * sizeof(*sorted));
    if (!sorted && n > 0)
        return -1;
    memcpy(sorted, v->u.dict.pairs, n * sizeof(*sorted));
    qsort(sorted, n, sizeof(*sorted), kv_cmp);

    if (buf_append(buf, "d", 1) != 0)
    {
        free(sorted);
        return -1;
    }
    for (size_t i = 0; i < n; i++)
    {
        if (dumps_val(sorted[i].key, buf) != 0 || dumps_val(sorted[i].val, buf) != 0)
        {
            free(sorted);
            return -1;
        }
    }
    free(sorted);
    return buf_append(buf, ";", 1);
}

static int dumps_none(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    (void) v;
    return buf_append(buf, "n", 1);
}

static int dumps_val(const struct bpickle_val *v, struct bpickle_buf *buf)
{
    if (!v)
        return -1;
    switch (v->type)
    {
        case BPICKLE_NONE:
            return dumps_none(v, buf);
        case BPICKLE_BOOL:
            return dumps_bool(v, buf);
        case BPICKLE_INT:
            return dumps_int(v, buf);
        case BPICKLE_FLOAT:
            return dumps_float(v, buf);
        case BPICKLE_BYTES:
            return dumps_bytes(v, buf);
        case BPICKLE_STR:
            return dumps_str(v, buf);
        case BPICKLE_LIST:
            return dumps_list(v, buf);
        case BPICKLE_DICT:
            return dumps_dict(v, buf);
    }
    return -1;
}

int bpickle_dumps(const struct bpickle_val *val, struct bpickle_buf *buf)
{
    return dumps_val(val, buf);
}

/* =========================================================================
 * Decoding (loads)
 * ====================================================================== */

/* All load_* functions take (data, len, pos) and advance *pos on success.
 * They return a newly allocated value, or NULL on error. */

typedef struct bpickle_val *(*load_fn)(const uint8_t *data, size_t len, size_t *pos);

/* Forward declaration */
static struct bpickle_val *loads_val(const uint8_t *data, size_t len, size_t *pos);

static struct bpickle_val *loads_none(const uint8_t *data, size_t len, size_t *pos)
{
    (void) data;
    (void) len;
    (*pos)++; /* consume 'n' */
    return bpickle_none();
}

static struct bpickle_val *loads_bool(const uint8_t *data, size_t len, size_t *pos)
{
    if (*pos + 2 > len)
        return NULL;
    uint8_t ch = data[*pos + 1];
    if (ch != '0' && ch != '1')
        return NULL;
    *pos += 2;
    return bpickle_bool(ch == '1');
}

/* Find the first ';' at or after data[*pos], return its index or SIZE_MAX */
static size_t find_semicolon(const uint8_t *data, size_t len, size_t pos)
{
    for (size_t i = pos; i < len; i++)
        if (data[i] == ';')
            return i;
    return SIZE_MAX;
}

static struct bpickle_val *loads_int(const uint8_t *data, size_t len, size_t *pos)
{
    size_t end = find_semicolon(data, len, *pos);
    if (end == SIZE_MAX)
        return NULL;

    /* data[*pos] == 'i', content is data[*pos+1 .. end-1] */
    char tmp[32];
    size_t clen = end - (*pos + 1);
    if (clen == 0 || clen >= sizeof(tmp))
        return NULL;
    memcpy(tmp, data + *pos + 1, clen);
    tmp[clen] = '\0';

    char *endptr;
    errno = 0;
    int64_t val = (int64_t) strtoll(tmp, &endptr, 10);
    if (errno || *endptr)
        return NULL;

    *pos = end + 1;
    return bpickle_int(val);
}

static struct bpickle_val *loads_float(const uint8_t *data, size_t len, size_t *pos)
{
    size_t end = find_semicolon(data, len, *pos);
    if (end == SIZE_MAX)
        return NULL;

    char tmp[64];
    size_t clen = end - (*pos + 1);
    if (clen == 0 || clen >= sizeof(tmp))
        return NULL;
    memcpy(tmp, data + *pos + 1, clen);
    tmp[clen] = '\0';

    char *endptr;
    errno = 0;
    double val = strtod(tmp, &endptr);
    if (errno || *endptr)
        return NULL;

    *pos = end + 1;
    return bpickle_float(val);
}

/*
 * Parse a length-prefixed payload: <prefix><N>:<N bytes>
 * On entry *pos points at the type prefix character.
 * Returns allocated copy of the payload or NULL on error.
 * Advances *pos past the payload.
 */
static uint8_t *loads_len_prefixed(const uint8_t *data, size_t len, size_t *pos, size_t *out_len)
{
    /* Find ':' */
    size_t colon = SIZE_MAX;
    for (size_t i = *pos + 1; i < len; i++)
    {
        if (data[i] == ':')
        {
            colon = i;
            break;
        }
    }
    if (colon == SIZE_MAX)
        return NULL;

    /* Parse the decimal length between the type char and ':' */
    char tmp[32];
    size_t nlen = colon - (*pos + 1);
    if (nlen == 0 || nlen >= sizeof(tmp))
        return NULL;
    memcpy(tmp, data + *pos + 1, nlen);
    tmp[nlen] = '\0';

    char *endptr;
    errno = 0;
    long long n = strtoll(tmp, &endptr, 10);
    if (errno || *endptr || n < 0)
        return NULL;

    size_t start = colon + 1;
    if (start + (size_t) n > len)
        return NULL;

    uint8_t *buf = malloc((size_t) n + 1);
    if (!buf)
        return NULL;
    memcpy(buf, data + start, (size_t) n);
    buf[(size_t) n] = 0;

    *out_len = (size_t) n;
    *pos = start + (size_t) n;
    return buf;
}

static struct bpickle_val *loads_bytes(const uint8_t *data, size_t len, size_t *pos)
{
    size_t plen;
    uint8_t *payload = loads_len_prefixed(data, len, pos, &plen);
    if (!payload)
        return NULL;

    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (!v)
    {
        free(payload);
        return NULL;
    }
    v->type = BPICKLE_BYTES;
    v->u.s.data = payload;
    v->u.s.len = plen;
    return v;
}

static struct bpickle_val *loads_str(const uint8_t *data, size_t len, size_t *pos)
{
    size_t plen;
    uint8_t *payload = loads_len_prefixed(data, len, pos, &plen);
    if (!payload)
        return NULL;

    struct bpickle_val *v = calloc(1, sizeof(*v));
    if (!v)
    {
        free(payload);
        return NULL;
    }
    v->type = BPICKLE_STR;
    v->u.s.data = payload;
    v->u.s.len = plen;
    return v;
}

static struct bpickle_val *loads_list_inner(const uint8_t *data, size_t len, size_t *pos)
{
    struct bpickle_val *list = bpickle_list_new();
    if (!list)
        return NULL;

    (*pos)++; /* consume 'l' or 't' */

    while (*pos < len && data[*pos] != ';')
    {
        struct bpickle_val *item = loads_val(data, len, pos);
        if (!item)
        {
            bpickle_val_free(list);
            return NULL;
        }
        if (bpickle_list_append(list, item) != 0)
        {
            bpickle_val_free(item);
            bpickle_val_free(list);
            return NULL;
        }
    }

    if (*pos >= len)
    {
        bpickle_val_free(list);
        return NULL; /* missing ';' */
    }
    (*pos)++; /* consume ';' */
    return list;
}

static struct bpickle_val *loads_list(const uint8_t *data, size_t len, size_t *pos)
{
    return loads_list_inner(data, len, pos);
}

/* Tuple on wire: decoded as a list (same as Python side) */
static struct bpickle_val *loads_tuple(const uint8_t *data, size_t len, size_t *pos)
{
    return loads_list_inner(data, len, pos);
}

static struct bpickle_val *loads_dict(const uint8_t *data, size_t len, size_t *pos)
{
    struct bpickle_val *dict = bpickle_dict_new();
    if (!dict)
        return NULL;

    (*pos)++; /* consume 'd' */

    while (*pos < len && data[*pos] != ';')
    {
        struct bpickle_val *key = loads_val(data, len, pos);
        if (!key)
        {
            bpickle_val_free(dict);
            return NULL;
        }

        /* Keys that come back as BYTES should be treated as STR,
         * matching the Python loads() default (as_is=False). */
        if (key->type == BPICKLE_BYTES)
            key->type = BPICKLE_STR;

        struct bpickle_val *val = loads_val(data, len, pos);
        if (!val)
        {
            bpickle_val_free(key);
            bpickle_val_free(dict);
            return NULL;
        }

        if (bpickle_dict_set(dict, key, val) != 0)
        {
            bpickle_val_free(key);
            bpickle_val_free(val);
            bpickle_val_free(dict);
            return NULL;
        }
    }

    if (*pos >= len)
    {
        bpickle_val_free(dict);
        return NULL; /* missing ';' */
    }
    (*pos)++; /* consume ';' */
    return dict;
}

static struct bpickle_val *loads_val(const uint8_t *data, size_t len, size_t *pos)
{
    if (*pos >= len)
        return NULL;

    switch (data[*pos])
    {
        case 'n':
            return loads_none(data, len, pos);
        case 'b':
            return loads_bool(data, len, pos);
        case 'i':
            return loads_int(data, len, pos);
        case 'f':
            return loads_float(data, len, pos);
        case 's':
            return loads_bytes(data, len, pos);
        case 'u':
            return loads_str(data, len, pos);
        case 'l':
            return loads_list(data, len, pos);
        case 't':
            return loads_tuple(data, len, pos);
        case 'd':
            return loads_dict(data, len, pos);
        default:
            return NULL;
    }
}

int bpickle_loads(const uint8_t *data, size_t len, struct bpickle_val **out)
{
    if (!data || len == 0 || !out)
        return -1;

    size_t pos = 0;
    struct bpickle_val *v = loads_val(data, len, &pos);
    if (!v)
        return -1;

    *out = v;
    return 0;
}
