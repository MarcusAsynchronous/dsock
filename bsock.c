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
    struct hvfptr *h = hdata(s, bsock_type);
    if(dsock_slow(!h)) return -1;
    struct bsockvfptrs *b = (struct bsockvfptrs*)h;
    return b->bsend(s, buf, len, deadline);
}

int brecv(int s, void *buf, size_t len, int64_t deadline) {
    struct hvfptr *h = hdata(s, bsock_type);
    if(dsock_slow(!h)) return -1;
    struct bsockvfptrs *b = (struct bsockvfptrs*)h;
    return b->brecv(s, buf, len, deadline);
}

