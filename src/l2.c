/* SPDX-License-Identifier: MPL-2.0 */
/* struct ifreq's real definition (net/if.h, via bits/ifreq.h) is guarded
 * behind glibc's __USE_MISC, which strict -std=c99 (this project's own
 * CMAKE_C_STANDARD 99 with CMAKE_C_EXTENSIONS OFF) does not define on its
 * own -- must be the literal first thing in the translation unit, before
 * any include, same fix and same rationale as tsn.c's own _DEFAULT_SOURCE
 * definition (SO_PRIORITY) and clock.c's _POSIX_C_SOURCE one. Confirmed
 * necessary by CI (gcc-12/clang-14 on ubuntu-22.04 both failed this
 * module's own SIOCGIFHWADDR/SIOCGIFINDEX ioctl calls with "storage size
 * of 'ifr' isn't known" / "incomplete type 'struct ifreq'" without it --
 * this project's macOS development environment did not catch it, since
 * this whole file's real (non-stub) implementation only compiles on
 * Linux in the first place). */
#define _DEFAULT_SOURCE

#include "rcp/l2.h"
#include "rcp/alloc.h"

#include "platform.h"

#include <stdlib.h>
#include <string.h>

/* ── Pure frame codec (no socket, no privilege, no Linux requirement) ──────
 *
 * Deliberately outside the __linux__ split below: this is plain byte
 * manipulation with no I/O and no platform dependency, so it builds and is
 * unit-testable on every platform this project targets, including the
 * non-Linux stub build (which has no real raw-socket implementation but
 * can still exercise this codec directly). See l2.h's own file header for
 * this frame's exact layout. */

//cfusa:req REQ-L2-001
rcp_bytes_t rcp_l2_frame_encode(const uint8_t dst_mac[6], const uint8_t src_mac[6],
                                 const uint8_t *avtpdu, size_t avtpdu_len)
{
    rcp_bytes_t out;
    uint8_t    *buf;

    out.data = NULL;
    out.len  = 0;

    buf = (uint8_t *)rcp_malloc(RCP_L2_HEADER_LEN + avtpdu_len);
    if (!buf) return out;

    memcpy(buf, dst_mac, 6);
    memcpy(buf + 6, src_mac, 6);
    buf[12] = (uint8_t)((RCP_L2_ETHERTYPE >> 8) & 0xFFu);
    buf[13] = (uint8_t)(RCP_L2_ETHERTYPE & 0xFFu);
    if (avtpdu_len > 0) memcpy(buf + RCP_L2_HEADER_LEN, avtpdu, avtpdu_len);

    out.data = buf;
    out.len  = RCP_L2_HEADER_LEN + avtpdu_len;
    return out;
}

//cfusa:req REQ-L2-002
bool rcp_l2_frame_decode(const uint8_t *frame, size_t frame_len,
                          uint8_t out_dst_mac[6], uint8_t out_src_mac[6],
                          const uint8_t **out_avtpdu, size_t *out_avtpdu_len)
{
    uint16_t ethertype;

    if (frame_len < RCP_L2_HEADER_LEN) return false;

    ethertype = (uint16_t)(((uint16_t)frame[12] << 8) | (uint16_t)frame[13]);
    if (ethertype != RCP_L2_ETHERTYPE) return false;

    memcpy(out_dst_mac, frame, 6);
    memcpy(out_src_mac, frame + 6, 6);
    *out_avtpdu     = frame + RCP_L2_HEADER_LEN;
    *out_avtpdu_len = frame_len - RCP_L2_HEADER_LEN;
    return true;
}

//cfusa:req REQ-L2-011
bool rcp_l2_mac_is_unicast(const uint8_t mac[6])
{
    return (mac[0] & 0x01u) == 0u;
}

#if defined(__linux__)

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* Largest single frame this transport will ever move: this module's own
 * 14-octet Ethernet header plus the largest AVTPDU avtp.h can produce
 * (a TSCF header -- the larger of the two variants -- plus its own
 * maximum payload). A real NIC's own MTU will in practice reject
 * anything larger than ~1500 octets of payload (jumbo frames aside) well
 * before this buffer size is ever reached; this is a receive-buffer
 * sizing bound, not a claim that every AVTPDU this constant admits will
 * actually fit on the wire. */
#define RCP_L2_MAX_FRAME \
    (RCP_L2_HEADER_LEN + RCP_AVTP_TSCF_HEADER_LEN + RCP_AVTP_TSCF_MAX_PAYLOAD)

/* recv()'s polling granularity -- see udp.c's own identical pattern and
 * this header's own file comment for why recv() polls rather than
 * blocking directly on the socket. */
#define RCP_L2_POLL_MS 20u

typedef struct rcp_l2_avtp_transport {
    rcp_avtp_transport_t base; /* first member: rcp_avtp_transport_t* <-> this cast */

    int     fd;
    bool    ok;
    int     ifindex;
    uint8_t dst_mac[6];
    uint8_t src_mac[6];

    rcp_mutex_t mu; /* protects closed */
    bool        closed;
} rcp_l2_avtp_transport_t;

//cfusa:req REQ-L2-006
static int l2_avtp_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)self;
    struct sockaddr_ll        sll;
    rcp_bytes_t                wire;
    bool                        closed_now;
    ssize_t                      n;

    rcp_mutex_lock(&l->mu);
    closed_now = l->closed;
    rcp_mutex_unlock(&l->mu);
    if (closed_now) return RCP_ERR_CLOSED;

    wire = rcp_l2_frame_encode(l->dst_mac, l->src_mac, frame, frame_len);
    if (!wire.data) return RCP_ERR_CLOSED; /* allocation failure */

    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(RCP_L2_ETHERTYPE);
    sll.sll_ifindex  = l->ifindex;
    sll.sll_halen    = 6;
    memcpy(sll.sll_addr, l->dst_mac, 6);

    n = sendto(l->fd, wire.data, wire.len, 0, (struct sockaddr *)&sll, sizeof(sll));
    rcp_bytes_free(&wire);
    if (n < 0) return RCP_ERR_CLOSED;
    return RCP_OK;
}

//cfusa:req REQ-L2-007
static int l2_avtp_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                         uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)self;
    uint8_t                   *tmp;

    tmp = (uint8_t *)rcp_malloc(RCP_L2_MAX_FRAME);
    if (!tmp) return RCP_ERR_BUSY;

    for (;;) {
        bool         closed_now;
        fd_set        rfds;
        struct timeval tv;
        int             sel;

        rcp_mutex_lock(&l->mu);
        closed_now = l->closed;
        rcp_mutex_unlock(&l->mu);

        if (closed_now) {
            rcp_free(tmp);
            return RCP_ERR_CLOSED;
        }
        if (rcp_context_done(ctx)) {
            rcp_free(tmp);
            return RCP_ERR_TIMEOUT;
        }

        tv.tv_sec  = 0;
        tv.tv_usec = (long)(RCP_L2_POLL_MS * 1000u);
        if (ctx->has_deadline) {
            uint64_t now       = rcp_monotonic_ms();
            uint64_t remaining = (ctx->deadline_ms > now) ? (ctx->deadline_ms - now) : 0;
            if (remaining < RCP_L2_POLL_MS) {
                tv.tv_sec  = 0;
                tv.tv_usec = (long)(remaining * 1000u);
            }
        }

        FD_ZERO(&rfds);
        FD_SET(l->fd, &rfds);

        sel = select(l->fd + 1, &rfds, NULL, NULL, &tv);
        if (sel > 0 && FD_ISSET(l->fd, &rfds)) {
            struct sockaddr_ll from;
            socklen_t           flen = sizeof(from);
            ssize_t              n   = recvfrom(l->fd, tmp, RCP_L2_MAX_FRAME, 0,
                                                  (struct sockaddr *)&from, &flen);
            uint8_t               dst_mac[6];
            uint8_t                src_mac[6];
            const uint8_t          *payload;
            size_t                    payload_len;

            if (n < 0) continue; /* transient recv error: keep polling */

            /* AF_PACKET raw sockets, by default, loop a copy of every frame
             * this same socket itself transmits back into its own recv()
             * queue (marked PACKET_OUTGOING) -- not just genuinely received
             * frames. Without filtering this out, a transport would
             * immediately dequeue its own just-sent frame instead of
             * waiting for an actual reply from its peer. Filtered here
             * (rather than via the newer PACKET_IGNORE_OUTGOING sockopt,
             * Linux 4.20+) so this works on any kernel this project
             * targets, not just recent ones. */
            if (from.sll_pkttype == PACKET_OUTGOING) continue;

            if (!rcp_l2_frame_decode(tmp, (size_t)n, dst_mac, src_mac, &payload, &payload_len)) {
                /* Too short to be one of this module's own AVTP-over-
                 * Ethernet frames, or a different EtherType than
                 * RCP_L2_ETHERTYPE somehow reached this socket anyway
                 * (defense in depth -- socket()/bind() already filter on
                 * protocol, this is a second, explicit check, not a
                 * silent trust of that filtering alone). Dropped and
                 * re-polled, same reasoning as udp.c's own malformed-
                 * datagram handling. */
                continue;
            }

            if (payload_len > buf_cap) {
                /* Oversized frame: dropped, not left retrievable -- same
                 * "nothing left in the kernel's own receive queue to
                 * retry against once recvfrom() already consumed it"
                 * reasoning as udp.c's own oversized-datagram handling. */
                rcp_free(tmp);
                return RCP_ERR_BUSY;
            }
            if (payload_len > 0) memcpy(buf, payload, payload_len);
            *out_len = payload_len;
            rcp_free(tmp);
            return RCP_OK;
        }
        /* sel == 0 (poll slice elapsed) or sel < 0 (e.g. EINTR): loop back
         * and re-check closed/ctx before waiting again. */
    }
}

//cfusa:req REQ-L2-008
static int l2_avtp_close(rcp_avtp_transport_t *self)
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)self;

    rcp_mutex_lock(&l->mu);
    l->closed = true;
    rcp_mutex_unlock(&l->mu);
    return RCP_OK;
}

static void l2_avtp_destroy(rcp_avtp_transport_t *self)
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)self;
    if (l->fd >= 0) {
        close(l->fd);
        l->fd = -1;
    }
    rcp_mutex_destroy(&l->mu);
    rcp_free(l);
}

static const rcp_avtp_transport_vtable_t l2_avtp_vtable = {
    l2_avtp_send,
    l2_avtp_recv,
    l2_avtp_close,
    l2_avtp_destroy,
};

//cfusa:req REQ-L2-003
//cfusa:req REQ-L2-010
rcp_avtp_transport_t *rcp_l2_avtp_transport_new(const char *ifname, const uint8_t dst_mac[6],
                                                 bool time_sync_supported)
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)rcp_calloc(1, sizeof(*l));
    struct ifreq               ifr;
    struct sockaddr_ll          sll;
    size_t                        iflen;

    if (!l) return NULL;
    l->base.vt                  = &l2_avtp_vtable;
    l->base.refcount             = 1;
    l->base.time_sync_supported = time_sync_supported;
    l->fd                       = -1;
    rcp_mutex_init(&l->mu);
    memcpy(l->dst_mac, dst_mac, 6);

    iflen = ifname ? strlen(ifname) : 0;
    if (iflen == 0 || iflen >= sizeof(ifr.ifr_name)) return &l->base; /* ok() == false */

    /* CAP_NET_RAW (or root) is required to open this socket -- see this
     * module's own header doc comment. Without it, socket() fails here
     * and this transport is left not-ok(), same "construction never
     * returns NULL just because the underlying resource couldn't be
     * opened" contract udp.c's own dial()/bind() use. */
    l->fd = socket(AF_PACKET, SOCK_RAW, htons(RCP_L2_ETHERTYPE));
    if (l->fd < 0) return &l->base;

    memset(&ifr, 0, sizeof(ifr));
    memcpy(ifr.ifr_name, ifname, iflen);
    if (ioctl(l->fd, SIOCGIFINDEX, &ifr) < 0) {
        close(l->fd);
        l->fd = -1;
        return &l->base;
    }
    l->ifindex = ifr.ifr_ifindex;

    memset(&sll, 0, sizeof(sll));
    sll.sll_family   = AF_PACKET;
    sll.sll_protocol = htons(RCP_L2_ETHERTYPE);
    sll.sll_ifindex  = l->ifindex;
    if (bind(l->fd, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        close(l->fd);
        l->fd = -1;
        return &l->base;
    }

    /* Source MAC is read from the interface itself, never caller-supplied
     * -- see this module's own header doc comment for why. */
    memset(&ifr, 0, sizeof(ifr));
    memcpy(ifr.ifr_name, ifname, iflen);
    if (ioctl(l->fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(l->fd);
        l->fd = -1;
        return &l->base;
    }
    memcpy(l->src_mac, ifr.ifr_hwaddr.sa_data, 6);

    l->ok = true;
    return &l->base;
}

//cfusa:req REQ-L2-004
bool rcp_l2_avtp_transport_ok(rcp_avtp_transport_t *t)
{
    return ((rcp_l2_avtp_transport_t *)t)->ok;
}

//cfusa:req REQ-L2-005
bool rcp_l2_avtp_transport_local_mac(rcp_avtp_transport_t *t, uint8_t out_mac[6])
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)t;
    if (!l->ok) return false;
    memcpy(out_mac, l->src_mac, 6);
    return true;
}

#else /* !defined(__linux__) -- raw AF_PACKET/SOCK_RAW is Linux-specific,
       * see this module's own header doc comment; every non-Linux build
       * gets the same fail-cleanly stub udp.c's own Windows path uses. */

typedef struct rcp_l2_avtp_transport {
    rcp_avtp_transport_t base;
} rcp_l2_avtp_transport_t;

//cfusa:req REQ-L2-009
static int l2_stub_send(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len)
{
    (void)self; (void)frame; (void)frame_len;
    return RCP_ERR_CLOSED;
}

//cfusa:req REQ-L2-009
static int l2_stub_recv(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                         uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    (void)self; (void)ctx; (void)buf; (void)buf_cap; (void)out_len;
    return RCP_ERR_CLOSED;
}

//cfusa:req REQ-L2-009
static int l2_stub_close(rcp_avtp_transport_t *self)
{
    (void)self;
    return RCP_OK;
}

static void l2_stub_destroy(rcp_avtp_transport_t *self)
{
    rcp_free(self);
}

static const rcp_avtp_transport_vtable_t l2_stub_vtable = {
    l2_stub_send, l2_stub_recv, l2_stub_close, l2_stub_destroy,
};

//cfusa:req REQ-L2-003
rcp_avtp_transport_t *rcp_l2_avtp_transport_new(const char *ifname, const uint8_t dst_mac[6],
                                                 bool time_sync_supported)
{
    rcp_l2_avtp_transport_t *l = (rcp_l2_avtp_transport_t *)rcp_calloc(1, sizeof(*l));
    (void)ifname; (void)dst_mac;
    if (!l) return NULL;
    l->base.vt                  = &l2_stub_vtable;
    l->base.refcount             = 1;
    l->base.time_sync_supported = time_sync_supported;
    return &l->base;
}

//cfusa:req REQ-L2-009
bool rcp_l2_avtp_transport_ok(rcp_avtp_transport_t *t)
{
    (void)t;
    return false;
}

//cfusa:req REQ-L2-009
bool rcp_l2_avtp_transport_local_mac(rcp_avtp_transport_t *t, uint8_t out_mac[6])
{
    (void)t; (void)out_mac;
    return false;
}

#endif /* __linux__ */
