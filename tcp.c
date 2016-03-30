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

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dillsocks.h"
#include "utils.h"

static const int tcplistener_type_placeholder = 0;
static const void *tcplistener_type = &tcplistener_type_placeholder;

static void tcplistener_close(int s);
static void tcplistener_dump(int s);

static const struct sockvfptrs tcplistener_vfptrs = {
    tcplistener_close,
    tcplistener_dump,
    NULL,
    NULL
};

struct tcplistener {
    int fd;
    int port;
};

static const int tcpconn_type_placeholder = 0;
static const void *tcpconn_type = &tcpconn_type_placeholder;

static void tcpconn_close(int s);
static void tcpconn_dump(int s);
static int tcpconn_send(int s, struct iovec *iovs, int niovs,
    const struct sockctrl *inctrl, struct sockctrl *outctrl, int64_t deadline);
static int tcpconn_recv(int s, struct iovec *iovs, int niovs, size_t *outlen,
    const struct sockctrl *inctrl, struct sockctrl *outctrl, int64_t deadline);

static const struct sockvfptrs tcpconn_vfptrs = {
    tcpconn_close,
    tcpconn_dump,
    tcpconn_send,
    tcpconn_recv
};

struct tcpconn {
    int fd;
    ipaddr addr;
    /* Sender side. */
    uint8_t *txbuf;
    size_t txbuf_len;
    size_t txbuf_capacity;
    int tosender;
    int fromsender;
    int sender;
    /* Receiver side. */
    uint8_t *rxbuf;
    size_t rxbuf_len;
    size_t rxbuf_capacity;
};

static coroutine int tcpconn_sender(struct tcpconn *conn) {
    int rc;
    while(1) {
        /* Hand the buffer to the main object. */
        rc = chsend(conn->fromsender, NULL, 0, -1);
        if(dill_slow(rc == -1 && errno == EPIPE)) goto error1;
        if(dill_slow(rc == -1 && errno == ECANCELED)) goto error1;
        dill_assert(rc == 0);
        /* Wait till main object fills the buffer and hands it back. */
        rc = chrecv(conn->tosender, NULL, 0, -1);
        if(dill_slow(rc == -1 && errno == EPIPE)) goto error1;
        if(dill_slow(rc == -1 && errno == ECANCELED)) goto error1;
        dill_assert(rc == 0);
        /* Loop until all data in send buffer are sent. */
        uint8_t *pos = conn->txbuf;
        size_t len = conn->txbuf_len;
        while(len) {
            rc = fdout(conn->fd, -1);
            if(dill_slow(rc < 0)) goto error1;
            ssize_t sz = send(conn->fd, pos, len, MSG_NOSIGNAL);
            if(dill_slow(sz < 0)) goto error1;
            pos += sz;
            len -= sz;
        }
    }
error1:
    rc = chdone(conn->fromsender);
    dill_assert(rc == 0);
    return 0;
}

static void tcptune(int s) {
    /* Make the socket non-blocking. */
    int opt = fcntl(s, F_GETFL, 0);
    if (opt == -1)
        opt = 0;
    int rc = fcntl(s, F_SETFL, opt | O_NONBLOCK);
    dill_assert(rc != -1);
    /*  Allow re-using the same local address rapidly. */
    opt = 1;
    rc = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
    dill_assert(rc == 0);
    /* If possible, prevent SIGPIPE signal when writing to the connection
        already closed by the peer. */
#ifdef SO_NOSIGPIPE
    opt = 1;
    rc = setsockopt (s, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof (opt));
    dill_assert (rc == 0 || errno == EINVAL);
#endif
}

static void fdclose(int fd) {
    fdclean(fd);
    int rc = close(fd);
    dill_assert(rc == 0);
}

static int tcpconn_init(struct tcpconn *conn, int fd) {
    conn->fd = fd;
    /* Sender side. */
    conn->txbuf = NULL;
    conn->txbuf_len = 0;
    conn->txbuf_capacity = 0;
    conn->tosender = channel(0, 0);
    dill_assert(conn->tosender >= 0);
    conn->fromsender = channel(0, 0);
    dill_assert(conn->fromsender >= 0);
    conn->sender = go(tcpconn_sender(conn));
    /* Receiver side. */
    conn->rxbuf = NULL;
    conn->rxbuf_len = 0;
    conn->rxbuf_capacity = 0;
    return 0;
}

static void tcplistener_close(int s) {
    struct tcplistener *lst = sockdata(s, tcplistener_type);
    dill_assert(lst);
    fdclose(lst->fd);
    free(lst);
}

static void tcplistener_dump(int s) {
    struct tcplistener *lst = sockdata(s, tcplistener_type);
    dill_assert(lst);
    fprintf(stderr, "TCPLISTENER fd:%d port:%d\n", lst->fd, lst->port);
}

static void tcpconn_close(int s) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    dill_assert(conn);
    /* Sender side. */
    hclose(conn->sender);
    hclose(conn->tosender);
    hclose(conn->fromsender);
    free(conn->txbuf);
    /* Receiver side. */
    free(conn->rxbuf);
    /* Deallocte the entire object. */
    fdclose(conn->fd);
    free(conn);
}

static void tcpconn_dump(int s) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    dill_assert(conn);
    fprintf(stderr, "TCP fd:%d\n", conn->fd);
}

static int tcpconn_send(int s, struct iovec *iovs, int niovs,
      const struct sockctrl *inctrl, struct sockctrl *outctrl,
      int64_t deadline) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    if(dill_slow(!conn)) return -1;
    if(dill_slow(niovs < 0 || (niovs && !iovs))) {errno == EINVAL; return -1;}
    /* This protocol doesn't use control data. */
    if(dill_slow(inctrl || outctrl)) {errno == EINVAL; return -1;}
    /* Wait till sender coroutine hands us the send buffer. */
    int rc = chrecv(conn->fromsender, NULL, 0, deadline);
    if(dill_slow(rc < 0 && errno == EPIPE)) {errno = ECONNRESET; return -1;}
    if(dill_slow(rc < 0))
        return -1;
    /* Resize the send buffer so that the data fit it. */
    size_t len = 0;
    int i;
    for(i = 0; i != niovs; ++i)
        len += iovs[i].iov_len;
    if(dill_slow(conn->txbuf_capacity < len)) {
        void *newbuf = realloc(conn->txbuf, len);
        if(dill_slow(!newbuf)) {
            /* TODO: Eek! Now we own the buffer but the next invocation of 
                     tcp_send() won't know about it. */
            errno = ENOMEM;
            return -1;
        }
        conn->txbuf = newbuf;
        conn->txbuf_capacity = len;
    }
    /* Copy the data to the buffer. */
    uint8_t *pos = conn->txbuf;
    for(i = 0; i != niovs; ++i) {
        memcpy(pos, iovs[i].iov_base, iovs[i].iov_len);
        pos += iovs[i].iov_len;
    }
    conn->txbuf_len = len;
    /* Hand the buffer to the sender coroutine. */
    rc = chsend(conn->tosender, NULL, 0, -1);
    dill_assert(rc == 0); // ECANCELED ?
    return 0;
}

static int tcpconn_recv(int s, struct iovec *iovs, int niovs, size_t *outlen,
      const struct sockctrl *inctrl, struct sockctrl *outctrl,
      int64_t deadline) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    if(dill_slow(!conn)) return -1;
    if(dill_slow(!s || niovs < 0 || (niovs && !iovs))) {
        errno == EINVAL; return -1;}
    /* This protocol doesn't use control data. */
    if(dill_slow(inctrl || outctrl)) {errno == EINVAL; return -1;}
    /* Compute total size of the data requested. */
    size_t sz = 0;
    int i;
    for(i = 0; i != niovs; ++i)
        sz += iovs[i].iov_len;
    /* If there's not enough data in the buffer try to read them from
       the socket. */
    int result = 0;
    if(sz > conn->rxbuf_len) {
        /* Resize the buffer to be able to hold all the data. */
        if(dill_slow(sz > conn->rxbuf_capacity)) {
            uint8_t *newbuf = realloc(conn->rxbuf, sz);
            if(dill_slow(!newbuf)) {errno = ENOMEM; return -1;}
            conn->rxbuf = newbuf;
            conn->rxbuf_capacity = sz;
        }
        while(conn->rxbuf_len < sz) {
            int rc = fdin(conn->fd, deadline);
            if(dill_slow(rc < 0)) return -1;
            ssize_t nbytes = recv(conn->fd, conn->rxbuf + conn->rxbuf_len,
                sz - conn->rxbuf_len, 0);
            if(dill_slow(nbytes < 0)) return -1;
            conn->rxbuf_len += nbytes;
            /* If connection was closed by the peer. */
            if(dill_slow(!nbytes)) {
                sz = conn->rxbuf_len;
                result = ECONNRESET;
                break;
            }
        }
    }
    /* Copy the data from rx buffer to user-supplied buffer(s). */
    size_t offset = 0;
    for(i = 0; i != niovs; ++i) {
        if(dill_slow(offset + iovs[i].iov_len > sz)) {
            memcpy(iovs[i].iov_base, conn->rxbuf + offset, sz - offset);
            break;
        }
        memcpy(iovs[i].iov_base, conn->rxbuf + offset, iovs[i].iov_len);
        offset += iovs[i].iov_len;
    }
    /* Shift remaining data in the buffer to the beginning. */
    conn->rxbuf_len -= sz;
    memmove(conn->rxbuf, conn->rxbuf + sz, conn->rxbuf_len);
    if(outlen)
        *outlen = sz;
    if(dill_fast(!result))
        return 0;
    errno = result;
    return -1;
}

int tcplisten(const ipaddr *addr, int backlog) {
    int err;
    if(dill_slow(backlog < 0)) {errno = EINVAL; return -1;}
    /* Open listening socket. */
    int s = socket(ipfamily(addr), SOCK_STREAM, 0);
    if(dill_slow(s < 0)) return -1;
    tcptune(s);
    /* Start listening. */
    int rc = bind(s, ipsockaddr(addr), iplen(addr));
    if(dill_slow(rc != 0)) return -1;
    rc = listen(s, backlog);
    if(dill_slow(rc != 0)) return -1;
    /* If the user requested an ephemeral port,
       retrieve the port number assigned by the OS now. */
    int port = ipport(addr);
    if(port == 0) {
        ipaddr baddr;
        socklen_t len = sizeof(ipaddr);
        rc = getsockname(s, (struct sockaddr*)&baddr, &len);
        if(dill_slow(rc < 0)) {err = errno; goto error1;}
        port = ipport(&baddr);
    }
    /* Create the object. */
    struct tcplistener *lst = malloc(sizeof(struct tcplistener));
    if(dill_slow(!lst)) {errno = ENOMEM; goto error1;}
    lst->fd = s;
    lst->port = port;
    /* Bind the object to a sock handle. */
    int hndl = sock(tcplistener_type, 0, lst, &tcplistener_vfptrs);
    if(dill_slow(hndl < 0)) {err = errno; goto error2;}
    return hndl;
error2:
    free(lst);
error1:
    fdclose(s);
    errno = err;
    return -1;
}

int tcpaccept(int s, int64_t deadline) {
    int err;
    struct tcplistener *lst = sockdata(s, tcplistener_type);
    if(dill_slow(!lst)) return -1;
    /* Try to get new connection in a non-blocking way. */
    int as;
    ipaddr addr;
    while(1) {
        socklen_t addrlen;
        addrlen = sizeof(addr);
        as = accept(lst->fd, (struct sockaddr*)&addr, &addrlen);
        if(as >= 0)
            break;
        dill_assert(as == -1);
        if(dill_slow(errno != EAGAIN && errno != EWOULDBLOCK)) return -1;
        /* Wait till new connection is available. */
        int rc = fdin(lst->fd, deadline);
        if(dill_slow(rc < 0)) return -1;
    }
    /* Create the object. */
    tcptune(as);
    struct tcpconn *conn = malloc(sizeof(struct tcpconn));
    if(dill_slow(!conn)) {err = ENOMEM; goto error1;}
    int rc = tcpconn_init(conn, as);
    if(dill_slow(rc < 0)) {err = errno; goto error2;}
    conn->addr = addr;
    /* Bind the object to a sock handle. */
    int hndl = sock(tcpconn_type, SOCK_IN | SOCK_OUT, conn, &tcpconn_vfptrs);
    if(dill_slow(hndl < 0)) {err = errno; goto error2;}
    return hndl;
error2:
    free(conn);
error1:
    fdclose(s);
    errno = err;
    return -1;
}

int tcpconnect(const ipaddr *addr, int64_t deadline) {
    int err;
    /* Open a socket. */
    int s = socket(ipfamily(addr), SOCK_STREAM, 0);
    if(dill_slow(s < 0)) return -1;
    tcptune(s);
    /* Connect to the remote endpoint. */
    int rc = connect(s, ipsockaddr(addr), iplen(addr));
    if(rc != 0) {
        dill_assert(rc == -1);
        if(dill_slow(errno != EINPROGRESS)) return -1;
        rc = fdout(s, deadline);
        if(dill_slow(rc < 0)) return -1;
        socklen_t errsz = sizeof(err);
        rc = getsockopt(s, SOL_SOCKET, SO_ERROR, (void*)&err, &errsz);
        if(rc != 0) {err = errno; goto error1;}
        if(err != 0) goto error1;
    }
    /* Create the object. */
    struct tcpconn *conn = malloc(sizeof(struct tcpconn));
    if(!conn) {err = ENOMEM; goto error1;}
    rc = tcpconn_init(conn, s);
    if(dill_slow(rc < 0)) {err = errno; goto error2;}
    /* Bind the object to a sock handle. */
    int hndl = sock(tcpconn_type, SOCK_IN | SOCK_OUT, conn, &tcpconn_vfptrs);
    if(dill_slow(hndl < 0)) {err = errno; goto error2;}
    return hndl;
error2:
    free(conn);
error1:
    fdclose(s);
    errno = err;
    return -1;
}

int tcpport(int s) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    if(conn)
        return ipport(&conn->addr);
    struct tcplistener *lst = sockdata(s, tcplistener_type);
    if(dill_slow(!lst)) return -1;
    return lst->port;
}

int tcppeer(int s, ipaddr *addr) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    if(dill_slow(!conn)) return -1;
    if(dill_fast(addr))
        *addr = conn->addr;
    return 0;
}

int tcpclose(int s, int64_t deadline) {
    struct tcpconn *conn = sockdata(s, tcpconn_type);
    if(dill_slow(!conn)) return -1;
    /* Soft-cancel handshake with the sender coroutine. */
    int rc = chdone(conn->tosender);
    dill_assert(rc == 0);
    rc = chrecv(conn->fromsender, NULL, 0, deadline);
    int result = (rc == 0 || errno == EPIPE) ? 0 : errno;
    /* Deallocate the entire socket. */
    hclose(s);
    if(!result)
        return 0;
    errno = result;
    return -1;
}

