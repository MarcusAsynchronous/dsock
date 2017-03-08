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

#include "../dsock.h"

int main() {

    const char key[] = "01234567890123456789012345678901";
    const char badkey[] = "X1234567890123456789012345678901";
    
    /* Test normal communication. */
    int s[2];
    int rc = ipc_pair(s);
    assert(rc == 0);
    int log0 = btrace_start(s[0]);
    assert(log0 >= 0);
    int log1 = btrace_start(s[1]);
    assert(log1 >= 0);
    int pfx0 = pfx_start(log0);
    assert(pfx0 >= 0);
    int pfx1 = pfx_start(log1);
    assert(pfx1 >= 0);
    int nacl0 = nacl_start(pfx0, key, 32, -1);
    assert(nacl0 >= 0);
    int nacl1 = nacl_start(pfx1, key, 32, -1);
    assert(nacl1 >= 0);
    rc = msend(nacl0, "ABC", 3, -1);
    assert(rc == 0);
    rc = msend(nacl0, "DEFG", 4, -1);
    assert(rc == 0);
    char buf[10] = {0};
    ssize_t sz = mrecv(nacl1, buf, sizeof(buf), -1);
    assert(sz == 3);
    assert(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C');
    sz = mrecv(nacl1, buf, sizeof(buf), -1);
    assert(sz == 4);
    assert(buf[0] == 'D' && buf[1] == 'E' && buf[2] == 'F' && buf[3] == 'G');
    rc = msend(nacl1, "HIJ", 3, -1);
    assert(rc == 0);
    sz = mrecv(nacl0, buf, sizeof(buf), -1);
    assert(sz == 3);
    assert(buf[0] == 'H' && buf[1] == 'I' && buf[2] == 'J');
    rc = hclose(nacl1);
    assert(rc == 0);
    rc = hclose(nacl0);
    assert(rc == 0);

    /* Test communication with wrong key. */
    rc = ipc_pair(s);
    assert(rc == 0);
    log0 = btrace_start(s[0]);
    assert(log0 >= 0);
    log1 = btrace_start(s[1]);
    assert(log1 >= 0);
    pfx0 = pfx_start(log0);
    assert(pfx0 >= 0);
    pfx1 = pfx_start(log1);
    assert(pfx1 >= 0);
    nacl0 = nacl_start(pfx0, key, 32, -1);
    assert(nacl0 >= 0);
    nacl1 = nacl_start(pfx1, badkey, 32, -1);
    assert(nacl1 >= 0);
    rc = msend(nacl0, "ABC", 3, -1);
    assert(rc == 0);
    sz = mrecv(nacl1, buf, sizeof(buf), -1);
    assert(sz == -1 && errno == EACCES);
    rc = hclose(nacl1);
    assert(rc == 0);
    rc = hclose(nacl0);
    assert(rc == 0);

    return 0;
}

