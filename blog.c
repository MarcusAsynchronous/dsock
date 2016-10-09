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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsock.h"
#include "dsock.h"
#include "iov.h"
#include "utils.h"

DSOCK_UNIQUE_ID(blog_type);

static void blog_close(int s);
static int blog_bsendv(int s, const struct iovec *iov, size_t iovlen,
    int64_t deadline);
static int blog_brecvv(int s, const struct iovec *iov, size_t iovlen,
    int64_t deadline);

struct blog_sock {
    struct bsock_vfptrs vfptrs;
    int s;
};

int blog_start(int s) {
    /* Check whether underlying socket is a bytestream. */
    if(dsock_slow(!hdata(s, bsock_type))) return -1;
    /* Create the object. */
    struct blog_sock *obj = malloc(sizeof(struct blog_sock));
    if(dsock_slow(!obj)) {errno = ENOMEM; return -1;}
    obj->vfptrs.hvfptrs.close = blog_close;
    obj->vfptrs.type = blog_type;
    obj->vfptrs.bsendv = blog_bsendv;
    obj->vfptrs.brecvv = blog_brecvv;
    obj->s = s;
    /* Create the handle. */
    int h = handle(bsock_type, obj, &obj->vfptrs.hvfptrs);
    if(dsock_slow(h < 0)) {
        int err = errno;
        free(obj);
        errno = err;
        return -1;
    }
    return h;
}

int blog_stop(int s) {
    struct blog_sock *obj = hdata(s, bsock_type);
    if(dsock_slow(obj && obj->vfptrs.type != blog_type)) {
        errno = ENOTSUP; return -1;}
    int u = obj->s;
    free(obj);
    return u;
}

static int blog_bsendv(int s, const struct iovec *iov, size_t iovlen,
      int64_t deadline) {
    struct blog_sock *obj = hdata(s, bsock_type);
    dsock_assert(obj->vfptrs.type == blog_type);
    size_t len = iov_size(iov, iovlen);
    size_t i, j;
    fprintf(stderr, "handle: %-4d send %8zuB: 0x", s, len);
    for(i = 0; i != iovlen; ++i) {
        for(j = 0; j != iov[i].iov_len; ++j) {
            fprintf(stderr, "%02x", (int)((uint8_t*)iov[i].iov_base)[j]);
        }
    }
    fprintf(stderr, "\n");
    return bsendv(obj->s, iov, iovlen, deadline);
}

static int blog_brecvv(int s, const struct iovec *iov, size_t iovlen,
      int64_t deadline) {
    struct blog_sock *obj = hdata(s, bsock_type);
    dsock_assert(obj->vfptrs.type == blog_type);
    int rc = brecvv(obj->s, iov, iovlen, deadline);
    if(dsock_slow(rc < 0)) return -1;
    size_t len = iov_size(iov, iovlen);
    size_t i, j;
    fprintf(stderr, "handle: %-4d recv %8zuB: 0x", s, len);
    for(i = 0; i != iovlen; ++i) {
        for(j = 0; j != iov[i].iov_len; ++j) {
            fprintf(stderr, "%02x", (int)((uint8_t*)iov[i].iov_base)[j]);
        }
    }
    fprintf(stderr, "\n");
    return 0;
} 

static void blog_close(int s) {
    struct blog_sock *obj = hdata(s, bsock_type);
    dsock_assert(obj && obj->vfptrs.type == blog_type);
    int rc = hclose(obj->s);
    dsock_assert(rc == 0);
    free(obj);
}

