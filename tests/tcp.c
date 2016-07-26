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

#include <assert.h>

#include "../dsock.h"

coroutine void client(int port) {
    ipaddr addr;
    int rc = ipremote(&addr, "127.0.0.1", port, 0, -1);
    assert(rc == 0);
    int cs = tcpconnect(&addr, -1);
    assert(cs >= 0);

    rc = msleep(now() + 100);
    assert(rc == 0);

    char buf[16];
    size_t sz = tcprecv(cs, buf, 3, -1);
    assert(sz == 3 && buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');

    sz = tcpsend(cs, "456", 3, -1);
    assert(sz == 3);
    rc = tcpflush(cs, -1);
    assert(rc == 0);

    rc = hclose(cs);
    assert(rc == 0);
}

coroutine void client2(int port) {
    ipaddr addr;
    int rc = ipremote(&addr, "127.0.0.1", port, 0, -1);
    assert(rc == 0);
    int cs = tcpconnect(&addr, -1);
    assert(cs >= 0);
    rc = msleep(now() + 100);
    assert(rc == 0);
    rc = hclose(cs);
    assert(rc == 0);
}


int main() {
    char buf[16];

    ipaddr addr;
    int rc = iplocal(&addr, NULL, 5555, 0);
    assert(rc == 0);
    int ls = tcplisten(&addr, 10);
    assert(ls >= 0);

    go(client(5555));

    int as = tcpaccept(ls, -1);

    /* Test deadline. */
    int64_t deadline = now() + 30;
    size_t sz = tcprecv(as, buf, sizeof(buf), deadline);
    assert(sz == 0 && errno == ETIMEDOUT);
    int64_t diff = now() - deadline;
    assert(diff > -20 && diff < 20);

    sz = tcpsend(as, "ABC", 3, -1);
    assert(sz == 3);
    rc = tcpflush(as, -1);
    assert(rc == 0);

    sz = tcprecv(as, buf, sizeof(buf), -1);
    assert(sz == 3);
    assert(buf[0] == '4' && buf[1] == '5' && buf[2] == '6');

    rc = hclose(as);
    assert(rc == 0);
    rc = hclose(ls);
    assert(rc == 0);

    /* Test whether we perform correctly when faced with TCP pushback. */
    ls = tcplisten(&addr, 10);
    go(client2(5555));
    as = tcpaccept(ls, -1);
    assert(as >= 0);
    char buffer[2048];
    while(1) {
        size_t sz = tcpsend(as, buffer, 2048, -1);
        if(sz == 0 && errno == ECONNRESET)
            break;
        assert(sz > 0);
        rc = tcpflush(as, -1);
        if(rc < 0 && errno == ECONNRESET)
            break;
        assert(rc == 0);
    }
    rc = hclose(as);
    assert(rc == 0);
    rc = hclose(ls);
    assert(rc == 0);

    return 0;
}

