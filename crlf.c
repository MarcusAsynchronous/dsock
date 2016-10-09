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
#include <stdlib.h>

#include "bsock.h"
#include "dsock.h"
#include "iov.h"
#include "msock.h"
#include "utils.h"

DSOCK_UNIQUE_ID(crlf_type);

static void crlf_close(int s);
static int crlf_msendv(int s, const struct iovec *iov, size_t iovlen,
    int64_t deadline);
static ssize_t crlf_mrecvv(int s, const struct iovec *iov, size_t iovlen,
    int64_t deadline);

struct crlf_sock {
    struct msock_vfptrs vfptrs;
    int s;
};

int crlf_start(int s) {
    /* Check whether underlying socket is a bytestream. */
    if(dsock_slow(!hdata(s, bsock_type))) return -1;
    /* Create the object. */
    struct crlf_sock *obj = malloc(sizeof(struct crlf_sock));
    if(dsock_slow(!obj)) {errno = ENOMEM; return -1;}
    obj->vfptrs.hvfptrs.close = crlf_close;
    obj->vfptrs.type = crlf_type;
    obj->vfptrs.msendv = crlf_msendv;
    obj->vfptrs.mrecvv = crlf_mrecvv;
    obj->s = s;
    /* Create the handle. */
    int h = handle(msock_type, obj, &obj->vfptrs.hvfptrs);
    if(dsock_slow(h < 0)) {
        int err = errno;
        free(obj);
        errno = err;
        return -1;
    }
    return h;
}

int crlf_stop(int s, int64_t deadline) {
    struct crlf_sock *obj = hdata(s, msock_type);
    if(dsock_slow(obj && obj->vfptrs.type != crlf_type)) {
        errno = ENOTSUP; return -1;}
    int u = obj->s;
    free(obj);
    return u;
}

static int crlf_msendv(int s, const struct iovec *iov, size_t iovlen,
      int64_t deadline) {
    struct crlf_sock *obj = hdata(s, msock_type);
    dsock_assert(obj->vfptrs.type == crlf_type);
    struct iovec vec[iovlen + 1];
    iov_copy(vec, iov, iovlen);
    vec[iovlen].iov_base = (void*)"\r\n";
    vec[iovlen].iov_len = 2;
    int rc = bsendv(obj->s, vec, iovlen + 1, deadline);
    if(dsock_slow(rc < 0)) return -1;
    return 0;
}

static ssize_t crlf_mrecvv(int s, const struct iovec *iov, size_t iovlen,
      int64_t deadline) {
    struct crlf_sock *obj = hdata(s, msock_type);
    dsock_assert(obj->vfptrs.type == crlf_type);
    size_t row = 0;
    size_t column = 0;
    size_t sz = 0;
    char c = 0;
    char pc;
    while(1) {
        pc = c;
        int rc = brecv(obj->s, &c, 1, deadline);
        if(dsock_slow(rc < 0)) return -1;
        if(row >= 0) {
            ((char*)iov[row].iov_base)[column] = c;
            if(column == iov[row].iov_len) {
                ++row; column = 0;}
            ++column;
        }
        ++sz;
        if(pc == '\r' && c == '\n') return sz - 2;
    }
}

static void crlf_close(int s) {
    struct crlf_sock *obj = hdata(s, msock_type);
    dsock_assert(obj && obj->vfptrs.type == crlf_type);
    int rc = hclose(obj->s);
    dsock_assert(rc == 0);
    free(obj);
}

