/*

  Copyright (c) 2016 Martin Sustrik

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom
  the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.

*/

#include <string.h>

#include "dsock.h"
#include "iov.h"
#include "utils.h"

size_t iov_size(const struct iovec *iov, size_t iovlen) {
    size_t sz = 0;
    size_t i;
    for(i = 0; i != iovlen; ++i) sz += iov[i].iov_len;
    return sz;
}

void iov_copyallfrom(void *dst, const struct iovec *src, size_t srclen) {
    int i;
    for(i = 0; i != srclen; ++i) {
        memcpy(dst, src[i].iov_base, src[i].iov_len);
        dst = ((uint8_t*)dst) + src[i].iov_len;
    }
}

void iov_copyallto(const struct iovec *dst, size_t dstlen, const void *src) {
    int i;
    for(i = 0; i != dstlen; ++i) {
        memcpy(dst[i].iov_base, src, dst[i].iov_len);
        src = ((uint8_t*)src) + dst[i].iov_len;
    }
}

void iov_copy(struct iovec *dst, const struct iovec *src, size_t len) {
    size_t i;
    for(i = 0; i != len; ++i) dst[i] = src[i];
}

size_t iov_cut(struct iovec *dst, const struct iovec *src, size_t iovlen,
      size_t offset, size_t bytes) {
    /* Get rid of corner cases. */
    if(dsock_slow(bytes == 0)) return 0;
    dsock_assert(iovlen > 0);
    /* Skip irrelevant iovecs. */
    int i;
    for(i = 0; i != iovlen; i++) {
        if(src[i].iov_len > offset) break;
        offset -= src[i].iov_len;
    }
    /* First iovec with data. */
    dst[0].iov_base = ((uint8_t*)src[i].iov_base) + offset;
    dst[0].iov_len = src[i].iov_len - offset;
    /* Copy all iovecs with data. */
    int j = 0;
    while(1) {
        if(dst[j].iov_len >= bytes) break;
        bytes -= dst[j].iov_len;
        dst[++j] = src[++i];
        dsock_assert(i < iovlen);
    }
    /* Last iovec with data. */
    dst[j].iov_len = bytes;
    return j + 1;
}

void iov_copyfrom(void *dst, const struct iovec *src, size_t srclen,
      size_t offset, size_t bytes) {
    struct iovec vec[srclen];
    size_t veclen = iov_cut(vec, src, srclen, offset, bytes);
    iov_copyallfrom(dst, vec, veclen);
}

void iov_copyto(const struct iovec *dst, size_t dstlen, const void *src,
      size_t offset, size_t bytes) {
    struct iovec vec[dstlen];
    size_t veclen = iov_cut(vec, dst, dstlen, offset, bytes);
    iov_copyallto(vec, veclen, src);
}

/* Copies the data from vector src into the vector dst. dst must be large enough to hold the data
 * TODO: Uses a simple hack at the moment, could be more optimized */
int iov_deep_copy(const struct iovec *dst, size_t dst_iov_len, const struct iovec *src, size_t src_iov_len) {
    if (!dst || !src) {errno = EINVAL; return -1;}

    size_t dst_size = iov_size(dst, dst_iov_len);
    size_t src_size = iov_size(src, src_iov_len);
    if (dst_size < src_size) {errno = EINVAL; return -1;}

    void *temp_buf = malloc(src_size);
    if (!temp_buf) return -1;
    iov_copyallfrom(temp_buf, src, src_iov_len);
    iov_copyto(dst, dst_iov_len, temp_buf, 0, src_size);
    free(temp_buf);
    return 0;
}
