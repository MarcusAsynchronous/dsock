/*

  Copyright (c) 2017 Martin Sustrik

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

#include <assert.h>
#include <string.h>

#include "../dsock.h"

coroutine void client(void) {
    struct ipaddr addr;
    int rc = ipaddr_remote(&addr, "127.0.0.1", 5555, 0, -1);
    assert(rc == 0);
    int s = tcp_connect(&addr, -1);
    assert(s >= 0);

    int cs = pfx_attach(s);
    assert(cs >= 0);
    rc = msend(cs, "ABC", 3, -1);
    assert(rc == 0);
    char buf[3];
    ssize_t sz = mrecv(cs, buf, 3, -1);
    assert(sz == 3);
    assert(buf[0] == 'G' && buf[1] == 'H' && buf[2] == 'I');
    rc = msend(cs, "DEF", 3, -1);
    assert(rc == 0);
    int ts = pfx_detach(cs, -1);
    assert(ts >= 0);
    rc = hclose(ts);
    assert(rc == 0);
}

int main(void) {
    struct ipaddr addr;
    int rc = ipaddr_local(&addr, NULL, 5555, 0);
    assert(rc == 0);
    int ls = tcp_listen(&addr, 10);
    assert(ls >= 0);
    go(client());
    int as = tcp_accept(ls, NULL, -1);

    int cs = pfx_attach(as);
    assert(cs >= 0);
    char buf[16];
    ssize_t sz = mrecv(cs, buf, sizeof(buf), -1);
    assert(sz == 3);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = msend(cs, "GHI", 3, -1);
    assert(rc == 0);
    sz = mrecv(cs, buf, sizeof(buf), -1);
    assert(sz == 3);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    int ts = pfx_detach(cs, -1);
    assert(ts >= 0);
    rc = hclose(ts);
    assert(rc == 0);

    int h[2];
    rc = ipc_pair(h);
    assert(rc == 0);
    int s0 = pfx_attach(h[0]);
    assert(s0 >= 0);
    int s1 = pfx_attach(h[1]);
    assert(s1 >= 0);
    rc = msend(s0, "First", 5, -1);
    assert(rc == 0);
    rc = msend(s0, "Second", 6, -1);
    assert(rc == 0);
    rc = msend(s0, "Third", 5, -1);
    assert(rc == 0);
    rc = hdone(s0, -1);
    assert(rc == 0);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    assert(sz == 5 && memcmp(buf, "First", 5) == 0);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    assert(sz == 6 && memcmp(buf, "Second", 5) == 0);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    assert(sz == 5 && memcmp(buf, "Third", 5) == 0);
    sz = mrecv(s1, buf, sizeof(buf), -1);
    assert(sz < 0 && errno == EPIPE);
    rc = msend(s1, "Red", 3, -1);
    assert(rc == 0);
    rc = msend(s1, "Blue", 4, -1);
    assert(rc == 0);
    int ts1 = pfx_detach(s1, -1);
    assert(ts1 >= 0);
    sz = mrecv(s0, buf, sizeof(buf), -1);
    assert(sz == 3 && memcmp(buf, "Red", 3) == 0);
    sz = mrecv(s0, buf, sizeof(buf), -1);
    assert(sz == 4 && memcmp(buf, "Blue", 4) == 0);
    sz = mrecv(s0, buf, sizeof(buf), -1);
    assert(sz < 0 && errno == EPIPE);
    int ts0 = pfx_detach(s0, -1);
    assert(ts0 >= 0);
    rc = hclose(ts1);
    assert(rc == 0);
    rc = hclose(ts0);
    assert(rc == 0);

    return 0;
}

