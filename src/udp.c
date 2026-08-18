/* SPDX-License-Identifier: MPL-2.0 */
/* Must precede any system header: exposes the POSIX declarations used below
 * on glibc under strict -std=c99 (see clock.c for the same fix). */
#define _POSIX_C_SOURCE 200809L

#include "rcp/udp.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#define RCP_UDP_POSIX 1
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/* ── Annex J encapsulation sequence number codec (pure, socket-free) ──────
 *
 * Deliberately outside the RCP_UDP_POSIX split below: this is plain byte
 * manipulation with no I/O and no platform dependency, so it builds and is
 * unit-testable on every platform this project targets, including the
 * Windows stub build (which has no real socket implementation but can
 * still exercise this codec directly). See udp.h's own file header for
 * this field's exact layout and its public-secondary-source provenance
 * caveat. Byte-order helper is this TU's own copy, matching acf.c's/
 * avtp.c's house convention of not sharing a byte-order util across
 * modules. */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

//cfusa:req REQ-UDP-015
rcp_bytes_t rcp_udp_annexj_wrap(uint32_t seq, const uint8_t *avtpdu, size_t avtpdu_len)
{
    rcp_bytes_t out;
    uint8_t    *buf;

    out.data = NULL;
    out.len  = 0;

    buf = (uint8_t *)rcp_malloc(RCP_UDP_ANNEX_J_SEQ_LEN + avtpdu_len);
    if (!buf) return out;

    put_u32(buf, seq);
    if (avtpdu_len > 0) rcp_memcpy_bounded(buf + RCP_UDP_ANNEX_J_SEQ_LEN, avtpdu_len, avtpdu, avtpdu_len);

    out.data = buf;
    out.len  = RCP_UDP_ANNEX_J_SEQ_LEN + avtpdu_len;
    return out;
}

//cfusa:req REQ-UDP-016
bool rcp_udp_annexj_unwrap(const uint8_t *datagram, size_t datagram_len,
                            uint32_t *out_seq, const uint8_t **out_avtpdu,
                            size_t *out_avtpdu_len)
{
    if (datagram_len < RCP_UDP_ANNEX_J_SEQ_LEN) return false;

    *out_seq        = get_u32(datagram);
    *out_avtpdu     = datagram + RCP_UDP_ANNEX_J_SEQ_LEN;
    *out_avtpdu_len = datagram_len - RCP_UDP_ANNEX_J_SEQ_LEN;
    return true;
}

#if defined(RCP_UDP_POSIX)

/* Largest single AVTPDU this transport will ever move: a TSCF header
 * (the larger of the two variants avtp.h defines) plus its own maximum
 * payload. Any real datagram larger than this is not a frame this wire
 * layer could have produced. The Annex J encapsulation sequence number
 * (udp.h's own file header) adds RCP_UDP_ANNEX_J_SEQ_LEN more octets
 * ahead of the AVTPDU on the wire -- the raw datagram buffer must be
 * sized for that too, even though callers of this transport's own
 * send()/recv() never see it (it is stripped/prepended transparently). */
#define RCP_UDP_AVTP_MAX_FRAME \
    (RCP_UDP_ANNEX_J_SEQ_LEN + RCP_AVTP_TSCF_HEADER_LEN + RCP_AVTP_TSCF_MAX_PAYLOAD)

/* recv()'s polling granularity -- see the file header's discussion of why
 * recv() polls rather than blocking directly on the socket. */
#define RCP_UDP_AVTP_POLL_MS 20u

typedef struct rcp_udp_avtp_transport {
    rcp_avtp_transport_t base; /* first member: rcp_avtp_transport_t* <-> this cast */

    int  fd;
    bool ok;
    bool bound_mode; /* true: rcp_udp_avtp_transport_bind(); false: _dial() */

    rcp_mutex_t        mu; /* protects closed/peer/has_peer/send_seq/recv_seq */
    bool                closed;
    struct sockaddr_in  peer;
    bool                has_peer;

    /* Annex J encapsulation sequence number state (udp.h's own file
     * header): send_seq is this transport's own per-connection
     * monotonically-incrementing counter (wraps on overflow, uint32_t
     * arithmetic is well-defined modulo 2^32 in C99); recv_seq/
     * has_recv_seq back rcp_udp_avtp_transport_last_recv_seq(). */
    uint32_t            send_seq;
    uint32_t            recv_seq;
    bool                has_recv_seq;
} rcp_udp_avtp_transport_t;

//cfusa:req REQ-UDP-004
//cfusa:req REQ-UDP-005
//cfusa:req REQ-UDP-007
//cfusa:req REQ-UDP-008
//cfusa:req REQ-UDP-011
//cfusa:req REQ-UDP-017
static int udp_avtp_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)self;
    struct sockaddr_in dest;
    bool have_dest = false;
    uint32_t seq;
    rcp_bytes_t wrapped;
    ssize_t n;

    rcp_mutex_lock(&u->mu);
    if (u->closed) {
        rcp_mutex_unlock(&u->mu);
        return RCP_ERR_CLOSED;
    }
    if (u->bound_mode && u->has_peer) {
        dest      = u->peer;
        have_dest = true;
    }
    /* Annex J encapsulation sequence number: this connection's own
     * monotonically-incrementing counter (udp.h's own file header) --
     * assigned and advanced under the same lock that already serializes
     * this transport's other mutable state. */
    seq = u->send_seq++;
    rcp_mutex_unlock(&u->mu);

    if (u->bound_mode && !have_dest) return RCP_ERR_BUSY; /* no peer learned yet -- see file header */

    wrapped = rcp_udp_annexj_wrap(seq, frame, frame_len);
    if (!wrapped.data) return RCP_ERR_CLOSED; /* allocation failure -- RCP_UDP_ANNEX_J_SEQ_LEN
                                                * alone (4) means wrap() only ever returns a
                                                * NULL buffer here on malloc() failure */

    if (u->bound_mode) {
        n = sendto(u->fd, wrapped.data, wrapped.len, 0, (struct sockaddr *)&dest, sizeof(dest));
    } else {
        n = send(u->fd, wrapped.data, wrapped.len, 0);
    }
    rcp_bytes_free(&wrapped);
    if (n < 0) return RCP_ERR_CLOSED;
    return RCP_OK;
}

//cfusa:req REQ-UDP-006
//cfusa:req REQ-UDP-009
//cfusa:req REQ-UDP-010
//cfusa:req REQ-UDP-011
//cfusa:req REQ-UDP-012
//cfusa:req REQ-UDP-018
static int udp_avtp_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                          uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)self;
    uint8_t *tmp;

    tmp = (uint8_t *)rcp_malloc(RCP_UDP_AVTP_MAX_FRAME);
    if (!tmp) return RCP_ERR_BUSY;

    for (;;) {
        bool closed_now;
        fd_set rfds;
        struct timeval tv;
        int sel;

        rcp_mutex_lock(&u->mu);
        closed_now = u->closed;
        rcp_mutex_unlock(&u->mu);

        if (closed_now) {
            rcp_free(tmp);
            return RCP_ERR_CLOSED;
        }
        if (rcp_context_done(ctx)) {
            rcp_free(tmp);
            return RCP_ERR_TIMEOUT;
        }

        tv.tv_sec  = 0;
        tv.tv_usec = (long)(RCP_UDP_AVTP_POLL_MS * 1000u);
        if (ctx->has_deadline) {
            uint64_t now       = rcp_monotonic_ms();
            uint64_t remaining = (ctx->deadline_ms > now) ? (ctx->deadline_ms - now) : 0;
            if (remaining < RCP_UDP_AVTP_POLL_MS) {
                tv.tv_sec  = 0;
                tv.tv_usec = (long)(remaining * 1000u);
            }
        }

        FD_ZERO(&rfds);
        FD_SET(u->fd, &rfds);

        sel = select(u->fd + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(u->fd, &rfds)) {
            struct sockaddr_in from;
            socklen_t          flen = sizeof(from);
            ssize_t            n    = recvfrom(u->fd, tmp, RCP_UDP_AVTP_MAX_FRAME, 0,
                                                (struct sockaddr *)&from, &flen);
            uint32_t            seq;
            const uint8_t      *payload;
            size_t               payload_len;

            if (n < 0) continue; /* transient recv error: keep polling */

            if (!rcp_udp_annexj_unwrap(tmp, (size_t)n, &seq, &payload, &payload_len)) {
                /* Datagram shorter than the Annex J encapsulation sequence
                 * number itself: not a frame this wire layer could have
                 * produced. Dropped and re-polled, same "no delivery
                 * guarantee to fight" reasoning as the oversized-datagram
                 * case below -- there is nothing left in the kernel's
                 * socket buffer to retry against once recvfrom() already
                 * consumed it. */
                continue;
            }

            rcp_mutex_lock(&u->mu);
            if (u->bound_mode) {
                u->peer     = from;
                u->has_peer = true;
            }
            u->recv_seq     = seq;
            u->has_recv_seq = true;
            rcp_mutex_unlock(&u->mu);

            if (payload_len > buf_cap) {
                /* Oversized datagram: dropped, not left retrievable -- unlike
                 * avtp.c's own in-process loopback transport, a real UDP
                 * datagram is already gone from the kernel's socket buffer
                 * the moment recvfrom() reads it, so there is nothing left
                 * to leave queued. This matches UDP's own inherent
                 * no-delivery-guarantee contract rather than fighting it. */
                rcp_free(tmp);
                return RCP_ERR_BUSY;
            }
            if (payload_len > 0) rcp_memcpy_bounded(buf, buf_cap, payload, payload_len);
            *out_len = payload_len;
            rcp_free(tmp);
            return RCP_OK;
        }
        /* sel == 0 (poll slice elapsed) or sel < 0 (e.g. EINTR): loop back
         * and re-check closed/ctx before waiting again. */
    }
}

//cfusa:req REQ-UDP-011
//cfusa:req REQ-UDP-012
static int udp_avtp_close(rcp_avtp_transport_t *self)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)self;

    rcp_mutex_lock(&u->mu);
    u->closed = true;
    rcp_mutex_unlock(&u->mu);
    return RCP_OK;
}

static void udp_avtp_destroy(rcp_avtp_transport_t *self)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)self;
    if (u->fd >= 0) {
        close(u->fd);
        u->fd = -1;
    }
    rcp_mutex_destroy(&u->mu);
    rcp_free(u);
}

static const rcp_avtp_transport_vtable_t udp_avtp_vtable = {
    udp_avtp_send,
    udp_avtp_recv,
    udp_avtp_close,
    udp_avtp_destroy,
};

static rcp_udp_avtp_transport_t *udp_avtp_new_base(bool time_sync_supported)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)rcp_calloc(1, sizeof(*u));
    if (!u) return NULL;
    u->base.vt                  = &udp_avtp_vtable;
    u->base.refcount             = 1;
    u->base.time_sync_supported = time_sync_supported;
    u->fd                       = -1;
    rcp_mutex_init(&u->mu);
    return u;
}

//cfusa:req REQ-UDP-001
//cfusa:req REQ-UDP-003
//cfusa:req REQ-UDP-013
rcp_avtp_transport_t *rcp_udp_avtp_transport_dial(const char *host, uint16_t port,
                                                   bool time_sync_supported)
{
    rcp_udp_avtp_transport_t *u = udp_avtp_new_base(time_sync_supported);
    struct sockaddr_in         sa;
    if (!u) return NULL;
    u->bound_mode = false;

    u->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (u->fd < 0) return &u->base; /* ok() == false */

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        close(u->fd);
        u->fd = -1;
        return &u->base;
    }
    if (connect(u->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(u->fd);
        u->fd = -1;
        return &u->base;
    }

    u->ok = true;
    return &u->base;
}

//cfusa:req REQ-UDP-002
//cfusa:req REQ-UDP-003
//cfusa:req REQ-UDP-013
rcp_avtp_transport_t *rcp_udp_avtp_transport_bind(const char *addr, uint16_t port,
                                                   bool time_sync_supported)
{
    rcp_udp_avtp_transport_t *u = udp_avtp_new_base(time_sync_supported);
    struct sockaddr_in         sa;
    if (!u) return NULL;
    u->bound_mode = true;

    u->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (u->fd < 0) return &u->base;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    if (!addr || addr[0] == '\0') {
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        close(u->fd);
        u->fd = -1;
        return &u->base;
    }
    if (bind(u->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(u->fd);
        u->fd = -1;
        return &u->base;
    }

    u->ok = true;
    return &u->base;
}

//cfusa:req REQ-UDP-019
rcp_avtp_transport_t *rcp_udp_avtp_transport_dial_default_port(const char *host,
                                                                 bool time_sync_supported)
{
    return rcp_udp_avtp_transport_dial(host, RCP_UDP_ANNEX_J_CONTROL_PORT, time_sync_supported);
}

//cfusa:req REQ-UDP-019
rcp_avtp_transport_t *rcp_udp_avtp_transport_bind_default_port(const char *addr,
                                                                 bool time_sync_supported)
{
    return rcp_udp_avtp_transport_bind(addr, RCP_UDP_ANNEX_J_CONTROL_PORT, time_sync_supported);
}

//cfusa:req REQ-UDP-003
bool rcp_udp_avtp_transport_ok(rcp_avtp_transport_t *t)
{
    return ((rcp_udp_avtp_transport_t *)t)->ok;
}

//cfusa:req REQ-UDP-004
uint16_t rcp_udp_avtp_transport_port(rcp_avtp_transport_t *t)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)t;
    struct sockaddr_in         sa;
    socklen_t                  len = sizeof(sa);
    if (u->fd < 0 || getsockname(u->fd, (struct sockaddr *)&sa, &len) < 0) return 0;
    return ntohs(sa.sin_port);
}

//cfusa:req REQ-UDP-004
size_t rcp_udp_avtp_transport_addr_string(rcp_avtp_transport_t *t, char *buf, size_t buf_len)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)t;
    struct sockaddr_in         sa;
    socklen_t                  len = sizeof(sa);
    char                       ipbuf[INET_ADDRSTRLEN];
    int                        n;

    if (u->fd < 0 || buf_len == 0) return 0;
    if (getsockname(u->fd, (struct sockaddr *)&sa, &len) < 0) return 0;
    if (!inet_ntop(AF_INET, &sa.sin_addr, ipbuf, sizeof(ipbuf))) return 0;

    n = snprintf(buf, buf_len, "%s:%u", ipbuf, (unsigned)ntohs(sa.sin_port));
    if (n < 0) return 0;
    return ((size_t)n < buf_len) ? (size_t)n : buf_len - 1;
}

//cfusa:req REQ-UDP-018
uint32_t rcp_udp_avtp_transport_last_recv_seq(rcp_avtp_transport_t *t)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)t;
    uint32_t                   seq;

    rcp_mutex_lock(&u->mu);
    seq = u->has_recv_seq ? u->recv_seq : 0u;
    rcp_mutex_unlock(&u->mu);
    return seq;
}

#else /* !RCP_UDP_POSIX -- Windows: no winsock implementation yet, see ROADMAP.md */

typedef struct rcp_udp_avtp_transport {
    rcp_avtp_transport_t base;
} rcp_udp_avtp_transport_t;

//cfusa:req REQ-UDP-014
static int stub_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    (void)self; (void)frame; (void)frame_len;
    return RCP_ERR_CLOSED;
}

//cfusa:req REQ-UDP-014
static int stub_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                      uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    (void)self; (void)ctx; (void)buf; (void)buf_cap; (void)out_len;
    return RCP_ERR_CLOSED;
}

//cfusa:req REQ-UDP-014
static int stub_close(rcp_avtp_transport_t *self)
{
    (void)self;
    return RCP_OK;
}

static void stub_destroy(rcp_avtp_transport_t *self)
{
    rcp_free(self);
}

static const rcp_avtp_transport_vtable_t udp_stub_vtable = {
    stub_send, stub_recv, stub_close, stub_destroy,
};

static rcp_avtp_transport_t *stub_new(bool time_sync_supported)
{
    rcp_udp_avtp_transport_t *u = (rcp_udp_avtp_transport_t *)rcp_calloc(1, sizeof(*u));
    if (!u) return NULL;
    u->base.vt                  = &udp_stub_vtable;
    u->base.refcount             = 1;
    u->base.time_sync_supported = time_sync_supported;
    return &u->base;
}

//cfusa:req REQ-UDP-001
rcp_avtp_transport_t *rcp_udp_avtp_transport_dial(const char *host, uint16_t port,
                                                   bool time_sync_supported)
{
    (void)host; (void)port;
    return stub_new(time_sync_supported);
}

//cfusa:req REQ-UDP-002
rcp_avtp_transport_t *rcp_udp_avtp_transport_bind(const char *addr, uint16_t port,
                                                   bool time_sync_supported)
{
    (void)addr; (void)port;
    return stub_new(time_sync_supported);
}

//cfusa:req REQ-UDP-019
rcp_avtp_transport_t *rcp_udp_avtp_transport_dial_default_port(const char *host,
                                                                 bool time_sync_supported)
{
    return rcp_udp_avtp_transport_dial(host, RCP_UDP_ANNEX_J_CONTROL_PORT, time_sync_supported);
}

//cfusa:req REQ-UDP-019
rcp_avtp_transport_t *rcp_udp_avtp_transport_bind_default_port(const char *addr,
                                                                 bool time_sync_supported)
{
    return rcp_udp_avtp_transport_bind(addr, RCP_UDP_ANNEX_J_CONTROL_PORT, time_sync_supported);
}

//cfusa:req REQ-UDP-014
bool rcp_udp_avtp_transport_ok(rcp_avtp_transport_t *t)
{
    (void)t;
    return false;
}

//cfusa:req REQ-UDP-014
uint16_t rcp_udp_avtp_transport_port(rcp_avtp_transport_t *t)
{
    (void)t;
    return 0;
}

//cfusa:req REQ-UDP-014
size_t rcp_udp_avtp_transport_addr_string(rcp_avtp_transport_t *t, char *buf, size_t buf_len)
{
    (void)t; (void)buf; (void)buf_len;
    return 0;
}

//cfusa:req REQ-UDP-014
uint32_t rcp_udp_avtp_transport_last_recv_seq(rcp_avtp_transport_t *t)
{
    (void)t;
    return 0;
}

#endif /* RCP_UDP_POSIX */
