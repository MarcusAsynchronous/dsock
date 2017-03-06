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
#include <libdillimpl.h>
#include <stdint.h>
#include <stdlib.h>

#include "dsock.h"
#include "utils.h"

dsock_unique_id(pfx_type);

static void *pfx_hquery(struct hvfs *hvfs, const void *type);
static void pfx_hclose(struct hvfs *hvfs);
static int pfx_hdone(struct hvfs *hvfs);
static int pfx_msendl(struct msock_vfs *mvfs,
    struct iolist *first, struct iolist *last, int64_t deadline);
static ssize_t pfx_mrecvl(struct msock_vfs *mvfs,
    struct iolist *first, struct iolist *last, int64_t deadline);

struct pfx_sock {
    struct hvfs hvfs;
    struct msock_vfs mvfs;
    int s;
    unsigned int indone : 1;
    unsigned int outdone: 1;
    unsigned int inerr : 1;
    unsigned int outerr : 1;
};

static void *pfx_hquery(struct hvfs *hvfs, const void *type) {
    struct pfx_sock *obj = (struct pfx_sock*)hvfs;
    if(type == msock_type) return &obj->mvfs;
    if(type == pfx_type) return obj;
    errno = ENOTSUP;
    return NULL;
}

int pfx_start(int s) {
    int err;
    /* Check whether underlying socket is a bytestream. */
    if(dsock_slow(!hquery(s, bsock_type))) {err = errno; goto error1;}
    /* Create the object. */
    struct pfx_sock *obj = malloc(sizeof(struct pfx_sock));
    if(dsock_slow(!obj)) {err = ENOMEM; goto error1;}
    obj->hvfs.query = pfx_hquery;
    obj->hvfs.close = pfx_hclose;
    obj->hvfs.done = pfx_hdone;
    obj->mvfs.msendl = pfx_msendl;
    obj->mvfs.mrecvl = pfx_mrecvl;
    obj->s = -1;
    obj->indone = 0;
    obj->outdone = 0;
    obj->inerr = 0;
    obj->outerr = 0;
    /* Create the handle. */
    int h = hmake(&obj->hvfs);
    if(dsock_slow(h < 0)) {int err = errno; goto error2;}
    /* Make a private copy of the underlying socket. */
    obj->s = hdup(s);
    if(dsock_slow(obj->s < 0)) {err = errno; goto error3;}
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

static int pfx_hdone(struct hvfs *hvfs) {
    struct pfx_sock *obj = (struct pfx_sock*)hvfs;
    if(dsock_slow(obj->outdone)) {errno = EPIPE; return -1;}
    if(dsock_slow(obj->outerr)) {errno = ECONNRESET; return -1;}
    /* Send termination message. TODO: This should be done asynchronously. */
    uint64_t sz = 0xffffffffffffffff;
    int rc = bsend(obj->s, &sz, 8, -1);
    if(dsock_slow(rc < 0)) {obj->outerr = 1; return -1;}
    obj->outdone = 1;
    return 0;
}

int pfx_stop(int s, int64_t deadline) {
    int err;
    struct pfx_sock *obj = hquery(s, pfx_type);
    if(dsock_slow(!obj)) return -1;
    if(dsock_slow(obj->inerr || obj->outerr)) {err = ECONNRESET; goto error;}
    /* If not done already start the terminal handshake. */
    if(!obj->outdone) {
        int rc = pfx_hdone(&obj->hvfs);
        if(dsock_slow(rc < 0)) {err = errno; goto error;}
    }
    /* Drain incoming messages until termination message is received. */
    while(1) {
        ssize_t sz = pfx_mrecvl(&obj->mvfs, NULL, NULL, deadline);
        if(sz < 0 && errno == EPIPE) break;
        if(dsock_slow(sz < 0)) {err = errno; goto error;}
    }
    int u = obj->s;
    free(obj);
    return u;
error:
    pfx_hclose(&obj->hvfs);
    errno = err;
    return -1;
}

static int pfx_msendl(struct msock_vfs *mvfs,
      struct iolist *first, struct iolist *last, int64_t deadline) {
    struct pfx_sock *obj = dsock_cont(mvfs, struct pfx_sock, mvfs);
    if(dsock_slow(obj->outdone)) {errno = EPIPE; return -1;}
    if(dsock_slow(obj->outerr)) {errno = ECONNRESET; return -1;}
    uint8_t szbuf[8];
    size_t sz = 0;
    struct iolist *it;
    for(it = first; it; it = it->iol_next)
        sz += it->iol_len;
    dsock_putll(szbuf, (uint64_t)sz);
    struct iolist hdr = {szbuf, 8, first, 0};
    int rc = bsendl(obj->s, &hdr, last, deadline);
    if(dsock_slow(rc < 0)) {obj->outerr = 1; return -1;}
    return 0;
}

static ssize_t pfx_mrecvl(struct msock_vfs *mvfs,
      struct iolist *first, struct iolist *last, int64_t deadline) {
    struct pfx_sock *obj = dsock_cont(mvfs, struct pfx_sock, mvfs);
    if(dsock_slow(obj->indone)) {errno = EPIPE; return -1;}
    if(dsock_slow(obj->inerr)) {errno = ECONNRESET; return -1;}
    uint8_t szbuf[8];
    int rc = brecv(obj->s, szbuf, 8, deadline);
    if(dsock_slow(rc < 0)) {obj->inerr = 1; return -1;}
    uint64_t sz = dsock_getll(szbuf);
    /* Peer is terminating. */
    if(dsock_slow(sz == 0xffffffffffffffff)) {
        obj->indone = 1; errno = EPIPE; return -1;}
    /* Trim iolist to reflect the size of the message. */
    size_t rmn = sz;
    struct iolist *it = first;
    while(1) {
        if(it->iol_len >= rmn) break;
        rmn -= it->iol_len;
        it = it->iol_next;
        if(dsock_slow(!it)) {obj->inerr = 1; errno = EMSGSIZE; return -1;}
    }
    size_t old_len = it->iol_len;
    struct iolist *old_next = it->iol_next;
    it->iol_len = rmn;
    it->iol_next = NULL;
    rc = brecvl(obj->s, first, last, deadline);
    /* Get iolist to its original state. */
    it->iol_len = old_len;
    it->iol_next = old_next;
    if(dsock_slow(rc < 0)) {obj->inerr = 1; return -1;}
    return sz;
}

static void pfx_hclose(struct hvfs *hvfs) {
    struct pfx_sock *obj = (struct pfx_sock*)hvfs;
    if(dsock_fast(obj->s >= 0)) {
        int rc = hclose(obj->s);
        dsock_assert(rc == 0);
    }
    free(obj);
}

