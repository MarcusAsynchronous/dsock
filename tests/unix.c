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
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../dsock.h"

#define TESTADDR "unix.test"

coroutine void client(void) {
    int cs = unix_connect(TESTADDR, -1);
    assert(cs >= 0);
    int rc = msleep(-1);
    assert(rc == -1 && errno == ECANCELED);
    rc = hclose(cs);
    assert(rc == 0);
}

coroutine void client2(int s) {
    char buf[3];
    int rc = brecv(s, buf, sizeof(buf), -1);
    assert(rc == 0);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = bsend(s, "DEF", 3, -1);
    assert(rc == 0);
    rc = unix_stop(s, -1);
    /* Main coroutine closes this coroutine before it manages to read
       the pending data from the socket. */
    assert(rc == -1 && errno == ECANCELED);
}

coroutine void client3(int s) {
    char buf[3];
    int rc = brecv(s, buf, sizeof(buf), -1);
    assert(rc == 0);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    rc = brecv(s, buf, sizeof(buf), -1);
    assert(rc == -1 && errno == EPIPE);
    rc = bsend(s, "DEF", 3, -1);
    assert(rc == 0);
    rc = unix_stop(s, -1);
    assert(rc == 0);
}

coroutine void client4(int s) {
    /* This line should make us hit unix pushback. */
    int rc = msleep(now() + 100);
    assert(rc == 0);
    rc = unix_stop(s, now() + 100);
    assert(rc == -1 && errno == ETIMEDOUT);
}

int main() {
    char buf[16];

    /* Test deadline. */
    struct stat st;
    int rc = stat(TESTADDR, &st);
    if(rc == 0) assert(unlink(TESTADDR) == 0);
    int ls = unix_listen(TESTADDR, 10);
    assert(ls >= 0);
    int cr = go(client());
    assert(cr >= 0);
    int as = unix_accept(ls, -1);
    assert(as >= 0);
    int64_t deadline = now() + 30;
    ssize_t sz = sizeof(buf);
    rc = brecv(as, buf, sizeof(buf), deadline);
    assert(rc == -1 && errno == ETIMEDOUT);
    int64_t diff = now() - deadline;
    assert(diff > -20 && diff < 20);
    rc = brecv(as, buf, sizeof(buf), deadline);
    assert(rc == -1 && errno == ECONNRESET);
    rc = hclose(as);
    assert(rc == 0);
    rc = hclose(ls);
    assert(rc == 0);
    rc = hclose(cr);
    assert(rc == 0);

    /* Test simple data exchange. */
    int s[2];
    rc = unix_pair(s);
    assert(rc == 0);
    cr = go(client2(s[1]));
    assert(cr >= 0);
    rc = bsend(s[0], "ABC", 3, -1);
    assert(rc == 0);
    rc = brecv(s[0], buf, 3, -1);
    assert(rc == 0);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    rc = brecv(s[0], buf, sizeof(buf), -1);
    assert(rc == -1 && errno == EPIPE);
    rc = unix_stop(s[0], -1);
    assert(rc == 0);
    rc = hclose(cr);
    assert(rc == 0);

    /* Manual termination handshake. */
    rc = unix_pair(s);
    assert(rc == 0);
    cr = go(client3(s[1]));
    assert(cr >= 0);
    rc = bsend(s[0], "ABC", 3, -1);
    assert(rc == 0);
    rc = hdone(s[0]);
    assert(rc == 0);
    rc = brecv(s[0], buf, 3, -1);
    assert(rc == 0);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F');
    rc = brecv(s[0], buf, sizeof(buf), -1);
    assert(rc == -1 && errno == EPIPE);
    rc = hclose(s[0]);
    assert(rc == 0);
    rc = hclose(cr);
    assert(rc == 0);    

    /* Emulate a DoS attack. */
    rc = unix_pair(s);
    assert(rc == 0);
    cr = go(client4(s[1]));
    assert(cr >= 0);
    char buffer[2048];
    while(1) {
        rc = bsend(s[0], buffer, 2048, -1);
        if(rc == -1 && errno == ECONNRESET)
            break;
        assert(rc == 0);
    }
    rc = unix_stop(s[0], -1);
    assert(rc == -1 && errno == ECONNRESET);
    rc = hclose(cr);
    assert(rc == 0);

    /* Try some invalid inputs. */
    rc = unix_pair(s);
    assert(rc == 0);
    struct iolist iol1 = {(void*)"ABC", 3, NULL, 0};
    struct iolist iol2 = {(void*)"DEF", 3, NULL, 0};
    rc = bsendl(s[0], &iol1, &iol2, -1);
    assert(rc == -1 && errno == EINVAL);
    iol1.iol_next = &iol2;
    iol2.iol_next = &iol1;
    rc = bsendl(s[0], &iol1, &iol2, -1);
    assert(rc == -1 && errno == EINVAL);
    assert(iol1.iol_rsvd == 0);
    assert(iol2.iol_rsvd == 0);
    rc = hclose(s[0]);
    assert(rc == 0);
    rc = hclose(s[1]);
    assert(rc == 0);

    return 0;
}

