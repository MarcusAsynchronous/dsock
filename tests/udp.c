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

int main() {
    ipaddr addr1;
    int rc = iplocal(&addr1, NULL, 5555, 0);
    assert(rc == 0);
    int s1 = udp_socket(&addr1, NULL);
    assert(s1 >= 0);
    ipaddr addr2;
    rc = iplocal(&addr2, NULL, 5556, 0);
    assert(rc == 0);
    int s2 = udp_socket(&addr2, &addr1);
    assert(s2 >= 0);

    while(1) {
        rc = udp_send(s1, &addr2, "ABC", 3);
        assert(rc == 0);
        char buf[3];
        ssize_t sz = mrecv(s2, buf, sizeof(buf), now() + 100);
        if(sz < 0 && errno == ETIMEDOUT)
            continue;
        assert(sz == 3);
        break;
    }

    while(1) {
        rc = msend(s2, "DEF", 3, -1);
        assert(rc == 0);
        char buf[3];
        ipaddr addr;
        ssize_t sz = udp_recv(s1, &addr, buf, sizeof(buf), now() + 100);
        if(sz < 0 && errno == ETIMEDOUT)
            continue;
        assert(sz == 3);
        break;
    }

    rc = hclose(s2);
    assert(rc == 0);
    rc = hclose(s1);
    assert(rc == 0);

    return 0;
}

