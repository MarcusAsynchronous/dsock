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

#include "bsock.h"
#include "dsock.h"
#include "utils.h"

static const int bsock_type_placeholder = 0;
const void *bsock_type = &bsock_type_placeholder;

int bsend(int s, const void *buf, size_t len, int64_t deadline) {
    struct iovec iov;
    iov.iov_base = (void*)buf;
    iov.iov_len = len;
    return bsendmsg(s, &iov, 1, deadline);
}

int brecv(int s, void *buf, size_t len, int64_t deadline) {
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = len;
    return brecvmsg(s, &iov, 1, deadline);
}

int bsendmsg(int s, const struct iovec *iov, size_t iovlen, int64_t deadline) {
    struct hvfptr *h = hdata(s, bsock_type);
    if(dsock_slow(!h)) return -1;
    struct bsock_vfptrs *b = (struct bsock_vfptrs*)h;
    return b->bsendmsg(s, iov, iovlen, deadline);
}

int brecvmsg(int s, const struct iovec *iov, size_t iovlen, int64_t deadline) {
    struct hvfptr *h = hdata(s, bsock_type);
    if(dsock_slow(!h)) return -1;
    struct bsock_vfptrs *b = (struct bsock_vfptrs*)h;
    return b->brecvmsg(s, iov, iovlen, deadline);
}

