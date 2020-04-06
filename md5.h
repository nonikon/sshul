/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _MD5_H_
#define _MD5_H_

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t  bytes;
    uint32_t  a, b, c, d;
    uint8_t   buffer[64];
} md5_t;

void md5_init(md5_t *ctx);
void md5_update(md5_t *ctx, const void *data, size_t size);
void md5_final(uint8_t result[16], md5_t *ctx);

#endif /* _MD5_H_ */
