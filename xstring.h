/*
 * Copyright (C) 2019-2023 nonikon@qq.com.
 * All rights reserved.
 */

#ifndef _XSTRING_H_
#define _XSTRING_H_

#include <stddef.h>
#include <string.h>
#include <assert.h>

#ifdef HAVE_XCONFIG_H
#include "xconfig.h"
#else

#ifndef XSTR_ENABLE_EXTRA
#define XSTR_ENABLE_EXTRA       1
#endif

#ifndef XSTR_DEFAULT_CAPACITY
#define XSTR_DEFAULT_CAPACITY   64
#endif

#endif

typedef struct xstr xstr_t;

struct xstr {
    char* data;
    size_t size;
    size_t capacity;
};

void __xstr_capacity_expand(xstr_t* xs, size_t extra);

static inline void __xstr_ensure_capacity(xstr_t* xs, size_t extra) {
    /* 1 byte null-terminated */
    if (xs->size + extra + 1 > xs->capacity) {
        __xstr_capacity_expand(xs, extra + 1);
    }
}

void xstr_init_ex(xstr_t* xs, size_t capacity);

static inline void xstr_init(xstr_t* xs) {
    xstr_init_ex(xs, XSTR_DEFAULT_CAPACITY);
}

void xstr_init_with_ex(xstr_t* xs, const char* cstr, size_t size);

static inline void xstr_init_with(xstr_t* xs, const char* cstr) {
    xstr_init_with_ex(xs, cstr, strlen(cstr));
}

/* <xs> must be a pointer returned by <xstr_init_xxx>. */
void xstr_destroy(xstr_t* xs);

xstr_t* xstr_new_ex(size_t capacity);

static inline xstr_t* xstr_new() {
    return xstr_new_ex(XSTR_DEFAULT_CAPACITY);
}

xstr_t* xstr_new_with_ex(const char* cstr, size_t size);

static inline xstr_t* xstr_new_with(const char* cstr) {
    return xstr_new_with_ex(cstr, strlen(cstr));
}

/* <xs> must be a pointer returned by <xstr_new_xxx>. */
void xstr_free(xstr_t* xs);

static inline void xstr_append_ex(xstr_t* xs, const char* cstr, size_t size) {
    __xstr_ensure_capacity(xs, size);
    memcpy(xs->data + xs->size, cstr, size);
    xs->size += size;
    xs->data[xs->size] = '\0';
}

static inline void xstr_append(xstr_t* xs, const char* cstr) {
    xstr_append_ex(xs, cstr, strlen(cstr));
}

static inline void xstr_assign_at_ex(xstr_t* xs, size_t pos, const char* cstr, size_t size) {
    assert(pos <= xs->size);
    xs->size = pos;
    xstr_append_ex(xs, cstr, size);
}

static inline void xstr_assign_at(xstr_t* xs, size_t pos, const char* cstr) {
    xstr_assign_at_ex(xs, pos, cstr, strlen(cstr));
}

static inline void xstr_insert_ex(xstr_t* xs, size_t pos, const char* cstr, size_t size) {
    assert(pos <= xs->size);
    __xstr_ensure_capacity(xs, size);
    memmove(xs->data + pos + size, xs->data + pos, xs->size - pos); /* >>> */
    memcpy(xs->data + pos, cstr, size);
    xs->size += size;
    xs->data[xs->size] = '\0';
}

static inline void xstr_insert(xstr_t* xs, size_t pos, const char* cstr) {
    xstr_insert_ex(xs, pos, cstr, strlen(cstr));
}

static inline void xstr_erase(xstr_t* xs, size_t pos, size_t count) {
    assert(pos + count <= xs->size);
    xs->size -= count;
    memmove(xs->data + pos, xs->data + pos + count, xs->size - pos); /* <<< */
    xs->data[xs->size] = '\0';
}

static inline void xstr_erase_after(xstr_t* xs, size_t pos) {
    assert(pos <= xs->size);
    xs->size = pos;
    xs->data[pos] = '\0';
}

static inline void xstr_clear(xstr_t* xs) {
    xstr_erase_after(xs, 0);
}

static inline void xstr_assign_ex(xstr_t* xs, const char* cstr, size_t size) {
    xstr_assign_at_ex(xs, 0, cstr, size);
}

static inline void xstr_assign(xstr_t* xs, const char* cstr) {
    xstr_assign_ex(xs, cstr, strlen(cstr));
}

static inline void xstr_prepend_ex(xstr_t* xs, const char* cstr, size_t size) {
    xstr_insert_ex(xs, 0, cstr, size);
}

static inline void xstr_prepend(xstr_t* xs, const char* cstr) {
    xstr_prepend_ex(xs, cstr, strlen(cstr));
}

static inline void xstr_push_back(xstr_t* xs, char ch) {
    __xstr_ensure_capacity(xs, 1);
    xs->data[xs->size++] = ch;
    xs->data[xs->size] = '\0';
}

static inline void xstr_pop_back(xstr_t* xs) {
    assert(xs->size > 0);
    xs->data[--xs->size] = '\0';
}

static inline void xstr_append_str(xstr_t* dest, xstr_t* src) {
    xstr_append_ex(dest, src->data, src->size);
}

static inline void xstr_prepend_str(xstr_t* dest, xstr_t* src) {
    xstr_prepend_ex(dest, src->data, src->size);
}

static inline void xstr_assign_str(xstr_t* dest, xstr_t* src) {
    xstr_assign_ex(dest, src->data, src->size);
}

static inline void xstr_assign_str_at(xstr_t* dest, size_t pos, xstr_t* src) {
    xstr_assign_at_ex(dest, pos, src->data, src->size);
}

static inline void xstr_insert_str(xstr_t* dest, size_t pos, xstr_t* src) {
    xstr_insert_ex(dest, pos, src->data, src->size);
}

static inline char* xstr_data(xstr_t* xs) {
    return xs->data;
}

static inline char xstr_front(xstr_t* xs) {
    return xs->data[0];
}

static inline char xstr_back(xstr_t* xs) {
    return xs->data[xs->size - 1];
}

static inline int xstr_empty(xstr_t* xs) {
    return xs->size == 0;
}

static inline size_t xstr_size(xstr_t* xs) {
    return xs->size;
}

static inline size_t xstr_capacity(xstr_t* xs) {
    return xs->capacity;
}

#if XSTR_ENABLE_EXTRA

/* unsigned long -> string, return a pointer pointed to <buf>.
 * <buf> size 36 may be the best.
 * radix: 2 ~ 36.
 * ex:
 *     val = 65535, radix = 10 -> buf = "65535".
 *     val = 65535, radix = 16 -> buf = "FFFF".
 */
char* xultoa(char* buf, unsigned long val, unsigned radix);

/* string -> unsigned long. similar to <strtol>.
 * base: 2 ~ 36.
 */
unsigned long xatoul(const char* str, char** ep, unsigned base);

#endif // XSTR_ENABLE_EXTRA

#endif // _XSTRING_H_
