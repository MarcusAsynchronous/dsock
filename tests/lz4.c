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
#include <string.h>

#include "../dsock.h"

int main() {

    /* Test whether big chunk gets through. */
    int s[2];
    int rc = unixpair(s);
    assert(rc == 0);
    int l0 = lz4attach(s[0]);
    assert(l0 >= 0);
    int l1 = lz4attach(s[1]);
    assert(l1 >= 0);
    rc = bsend(l0, "123456789012345678901234567890", 30, -1);
    assert(rc == 0);
    uint8_t buf[30];
    rc = brecv(l1, buf, 30, -1);
    assert(rc == 0);
    assert(memcmp(buf, "123456789012345678901234567890", 30) == 0);
    rc = hclose(l1);
    assert(rc == 0);
    rc = hclose(l0);
    assert(rc == 0);

    return 0;
}

