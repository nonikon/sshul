/*
 * Copyright (C) 2019-2023 nonikon@qq.com.
 * All rights reserved.
 */

#include <stdlib.h>

#include "xstring.h"

void __xstr_capacity_expand(xstr_t* xs, size_t size)
{
    size_t new_cap = xs->capacity << 1;
    void* new_data;

    size += xs->size;
    if (new_cap < size) {
        new_cap = size;
    }
    new_data = realloc(xs->data, new_cap);
    if (new_data) {
        xs->data = new_data;
        xs->capacity = new_cap;
    } else {
        abort();
    }
}

void xstr_init_ex(xstr_t* xs, size_t capacity)
{
    assert(capacity > 0);
    xs->capacity = capacity;
    xs->data = malloc(xs->capacity);
    if (xs->data) {
        xs->size = 0;
        xs->data[0] = '\0';
    } else {
        abort();
    }
}

void xstr_init_with_ex(xstr_t* xs, const char* cstr, size_t size)
{
    xs->capacity = size + 1;
    xs->data = malloc(xs->capacity);
    if (xs->data) {
        memcpy(xs->data, cstr, size);
        xs->size = size;
        xs->data[size] = '\0';
    } else {
        abort();
    }
}

void xstr_destroy(xstr_t* xs)
{
    free(xs->data);
}

xstr_t* xstr_new_ex(size_t capacity)
{
    xstr_t* xs = malloc(sizeof(xstr_t));

    if (xs) {
        xstr_init_ex(xs, capacity);
        return xs;
    }
    abort();
    return NULL;
}

xstr_t* xstr_new_with_ex(const char* cstr, size_t size)
{
    xstr_t* xs = malloc(sizeof(xstr_t));

    if (xs) {
        xstr_init_with_ex(xs, cstr, size);
        return xs;
    }
    abort();
    return NULL;
}

void xstr_free(xstr_t* xs)
{
    if (xs) {
        free(xs->data);
        free(xs);
    }
}

#if XSTR_ENABLE_EXTRA
static const char __i2c_table[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z'
};

char* xultoa(char* buf, unsigned long val, unsigned radix)
{
    int l = 0;
    int r = 0;
    char c;

    if (!val) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    do {
        buf[r++] = __i2c_table[val % radix];
        val /= radix;
    } while (val);

    buf[r--] = '\0';
    /* string reverse */
    while (l < r) {
        c = buf[l];
        buf[l++] = buf[r];
        buf[r--] = c;
    }

    return buf;
}

static const unsigned char __c2i_table[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*0~15*/
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*16~31*/
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*32~47*/
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1, /*48~63*/
    -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, /*64~79*/
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1, /*80~95*/
    -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, /*64~79*/
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1, /*112~127*/
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

unsigned long xatoul(const char* str, char** ep, unsigned base)
{
    unsigned long acc = 0;
    unsigned v;

    while (*str) {
        v = __c2i_table[(unsigned char)*str];
        if (v >= base) {
            break;
        }
        acc = acc * base + v;
        str = str + 1;
    }

    if (ep) {
        *ep = (char*) str;
    }
    return acc;
}
#endif // XSTR_ENABLE_EXTRA
