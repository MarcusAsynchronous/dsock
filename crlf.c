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

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "dsockimpl.h"
#include "iov.h"
#include "utils.h"

dsock_unique_id(crlf_type);

static void *crlf_hquery(struct hvfs *hvfs, const void *type);
static void crlf_hclose(struct hvfs *hvfs);
static int crlf_hdone(struct hvfs *hvfs);
static int crlf_msendl(struct msock_vfs *mvfs,
    struct iolist *first, struct iolist *last, int64_t deadline);
static ssize_t crlf_mrecvl(struct msock_vfs *mvfs,
    struct iolist *first, struct iolist *last, int64_t deadline);

struct crlf_sock {
    struct hvfs hvfs;
    struct msock_vfs mvfs;
    int u;
    /* Given that we are doing one recv call per byte, let's cache the pointer
       to bsock interface of the underlying socket to make it faster. */
    struct bsock_vfs *uvfs;
    unsigned int indone : 1;
    unsigned int outdone : 1;
    unsigned int inerr : 1;
    unsigned int outerr : 1;
};

static void *crlf_hquery(struct hvfs *hvfs, const void *type) {
    struct crlf_sock *obj = (struct crlf_sock*)hvfs;
    if(type == msock_type) return &obj->mvfs;
    if(type == crlf_type) return obj;
    errno = ENOTSUP;
    return NULL;
}

int crlf_start(int s) {
    int err;
    /* Create the object. */
    struct crlf_sock *obj = malloc(sizeof(struct crlf_sock));
    if(dsock_slow(!obj)) {err = ENOMEM; goto error1;}
    obj->hvfs.query = crlf_hquery;
    obj->hvfs.close = crlf_hclose;
    obj->hvfs.done = crlf_hdone;
    obj->mvfs.msendl = crlf_msendl;
    obj->mvfs.mrecvl = crlf_mrecvl;
    obj->u = -1;
    obj->uvfs = hquery(s, bsock_type);
    if(dsock_slow(!obj->uvfs)) {err = errno; goto error2;}
    obj->indone = 0;
    obj->outdone = 0;
    obj->inerr = 0;
    obj->outerr = 0;
    /* Create the handle. */
    int h = hmake(&obj->hvfs);
    if(dsock_slow(h < 0)) {err = errno; goto error2;}
    /* Make a private copy of the underlying socket. */
    obj->u = hdup(s);
    if(dsock_slow(obj->u < 0)) {err = errno; goto error3;}
    int rc = hclose(s);
    dsock_assert(rc == 0);
    return h;
error3:
    rc = hclose(h);
    dsock_assert(rc == 0);
error2:
    free(obj);
error1:
    errno = err;
    return -1;
}

static int crlf_hdone(struct hvfs *hvfs) {
    struct crlf_sock *obj = (struct crlf_sock*)hvfs;
    if(dsock_slow(obj->outdone)) {errno = EPIPE; return -1;}
    if(dsock_slow(obj->outerr)) {errno = ECONNRESET; return -1;}
    /* Send termination message. TODO: Do this asynchronously. */
    int rc = bsend(obj->u, "\r\n", 2, -1);
    if(dsock_slow(rc < 0)) {obj->outerr = 1; return -1;}
    obj->outdone = 1;
    return 0;
}

int crlf_stop(int s, int64_t deadline) {
    int err;
    struct crlf_sock *obj = hquery(s, crlf_type);
    if(dsock_slow(!obj)) return -1;
    if(dsock_slow(obj->inerr || obj->outerr)) {err = ECONNRESET; goto error;}
    /* If not done already start the terminal handshake. */
    if(!obj->outdone) {
        int rc = crlf_hdone(&obj->hvfs);
        if(dsock_slow(rc < 0)) {err = errno; goto error;}
    }
    /* Drain incoming messages until termination message is received. */
    while(1) {
        ssize_t sz = crlf_mrecvl(&obj->mvfs, NULL, NULL, deadline);
        if(sz < 0 && errno == EPIPE) break;
        if(dsock_slow(sz < 0)) {err = errno; goto error;}
    }
    int u = obj->u;
    free(obj);
    return u;
error:
    crlf_hclose(&obj->hvfs);
    errno = err;
    return -1;
}

static int crlf_msendl(struct msock_vfs *mvfs,
      struct iolist *first, struct iolist *last, int64_t deadline) {
    struct crlf_sock *obj = dsock_cont(mvfs, struct crlf_sock, mvfs);
    if(dsock_slow(obj->outdone)) {errno = EPIPE; return -1;}
    if(dsock_slow(obj->outerr)) {errno = ECONNRESET; return -1;}
    /* Make sure that message doesn't contain CRLF sequence. */
    uint8_t c = 0;
    size_t sz = 0;
    struct iolist *it;
    for(it = first; it; it = it->iol_next) {
        int i;
        for(i = 0; i != it->iol_len; ++i) {
            uint8_t c2 = ((uint8_t*)it->iol_base)[i];
            if(dsock_slow(c == '\r' && c2 == '\n')) {
                obj->outerr = 1; errno = EINVAL; return -1;}
            c = c2;
        }
        sz += it->iol_len;
    }
    /* Can't send empty line. Empty line is used as protocol terminator. */
    if(dsock_slow(sz == 0)) {obj->outerr = 1; errno = EINVAL; return -1;}
    struct iolist iol = {(void*)"\r\n", 2, NULL};
    last->iol_next = &iol;
    int rc = obj->uvfs->bsendl(obj->uvfs, first, &iol, deadline);
    last->iol_next = NULL;
    if(dsock_slow(rc < 0)) {obj->outerr = 1; return -1;}
    return 0;
}

static ssize_t crlf_mrecvl(struct msock_vfs *mvfs,
      struct iolist *first, struct iolist *last, int64_t deadline) {
    /* TODO: The buffer must accommodate CRLF sequence.
       That should not be the case. */
    struct crlf_sock *obj = dsock_cont(mvfs, struct crlf_sock, mvfs);
    if(dsock_slow(obj->indone)) {errno = EPIPE; return -1;}
    if(dsock_slow(obj->inerr)) {errno = ECONNRESET; return -1;}
    size_t sz = 0;
    char c1 = 0;
    char c2 = 0;
    struct iolist iol = {&c2, 1, NULL};
    struct iolist *it = first;
    size_t column = 0;
    while(c1 != '\r' || c2 != '\n') {
        c1 = c2;
        /* Message is longer than the buffer. */
        if(!it) {obj->inerr = 1; errno = EMSGSIZE; return -1;}
        /* Get one character. */
        int rc = obj->uvfs->brecvl(obj->uvfs, &iol, &iol, deadline);
        if(dsock_slow(rc < 0)) {obj->inerr = -1; return -1;}
        /* Put the character into the user's buffer. */
        if(it->iol_base) ((char*)it->iol_base)[column] = c2;
        ++column;
        if(column == it->iol_len) {
            column = 0;
            it = it->iol_next;
        }
        ++sz;
    }
    /* Empty line means that peer is terminating. */
    if(dsock_slow(sz == 2)) {obj->indone = 1; errno = EPIPE; return -1;}
    return sz - 2;
}

static void crlf_hclose(struct hvfs *hvfs) {
    struct crlf_sock *obj = (struct crlf_sock*)hvfs;
    if(dsock_fast(obj->u >= 0)) {
        int rc = hclose(obj->u);
        dsock_assert(rc == 0);
    }
    free(obj);
}

