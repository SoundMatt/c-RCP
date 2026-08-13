/* SPDX-License-Identifier: MPL-2.0 */
/* Loopback integration tests for the UDP AVTPDU transport. Skips
 * (TEST_IGNORE) rather than failing on platforms where
 * rcp_udp_avtp_transport_ok() reports the transport isn't available
 * (currently: Windows, which only has the stub implementation -- see
 * ROADMAP.md). */
//cfusa:test REQ-UDP-001
//cfusa:test REQ-UDP-002
//cfusa:test REQ-UDP-003
//cfusa:test REQ-UDP-004
//cfusa:test REQ-UDP-005
//cfusa:test REQ-UDP-006
//cfusa:test REQ-UDP-007
//cfusa:test REQ-UDP-008
//cfusa:test REQ-UDP-009
//cfusa:test REQ-UDP-010
//cfusa:test REQ-UDP-011
//cfusa:test REQ-UDP-012
//cfusa:test REQ-UDP-013
//cfusa:test REQ-UDP-014
//cfusa:test REQ-UDP-015
//cfusa:test REQ-UDP-016
//cfusa:test REQ-UDP-017
//cfusa:test REQ-UDP-018
//cfusa:test REQ-UDP-019
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/rcp.h>
#include <rcp/udp.h>

#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread_t;
static test_thread_t test_thread_spawn(DWORD(WINAPI *fn)(void *), void *arg)
{
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
static void test_thread_join(test_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef pthread_t test_thread_t;
static test_thread_t test_thread_spawn(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void test_thread_join(test_thread_t t) { pthread_join(t, NULL); }
#endif

void setUp(void) {}
void tearDown(void) {}

static rcp_bytes_t make_ntscf_frame(uint8_t seed)
{
    static const uint8_t   mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    rcp_avtp_ntscf_header_t hdr   = {0};
    uint8_t                 payload[4];
    size_t                   i;

    hdr.sv          = 1;
    hdr.version      = 0;
    hdr.sequence_num = seed;
    hdr.stream_id    = rcp_stream_id_make(mac, 1);

    for (i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(seed + i);

    return rcp_avtp_encode_ntscf(&hdr, payload, sizeof(payload));
}

/* Bind a server-side transport on an OS-assigned ephemeral loopback port,
 * or IGNORE the calling test if the platform has no real implementation
 * (Windows). Never returns NULL to a caller that didn't first check
 * ok(). */
static rcp_avtp_transport_t *bind_or_ignore(void)
{
    rcp_avtp_transport_t *srv = rcp_udp_avtp_transport_bind("127.0.0.1", 0, false);
    if (!rcp_udp_avtp_transport_ok(srv)) {
        rcp_avtp_transport_release(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return NULL;
    }
    return srv;
}

static void test_dial_send_recv_roundtrip(void)
{
    rcp_avtp_transport_t *srv;
    rcp_avtp_transport_t *cli;
    rcp_context_t          ctx;
    rcp_bytes_t             frame;
    uint8_t                 buf[256];
    size_t                   out_len = 0;
    uint16_t                 port;

    srv = bind_or_ignore();
    if (!srv) return;
    port = rcp_udp_avtp_transport_port(srv);

    cli = rcp_udp_avtp_transport_dial("127.0.0.1", port, false);
    TEST_ASSERT_TRUE(rcp_udp_avtp_transport_ok(cli));

    frame = make_ntscf_frame(7);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(cli, frame.data, frame.len));

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(frame.len, out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, buf, frame.len);

    rcp_bytes_free(&frame);
    rcp_avtp_transport_release(cli);
    rcp_avtp_transport_release(srv);
}

static void test_bind_learns_peer_and_replies(void)
{
    rcp_avtp_transport_t *srv;
    rcp_avtp_transport_t *cli;
    rcp_context_t          ctx;
    rcp_bytes_t             req_frame;
    rcp_bytes_t             reply_frame;
    uint8_t                 buf[256];
    size_t                   out_len = 0;
    uint16_t                 srv_port;

    srv = bind_or_ignore();
    if (!srv) return;
    srv_port = rcp_udp_avtp_transport_port(srv);

    cli = rcp_udp_avtp_transport_dial("127.0.0.1", srv_port, false);
    TEST_ASSERT_TRUE(rcp_udp_avtp_transport_ok(cli));

    /* Before srv has ever received a datagram, it has no learned peer. */
    reply_frame = make_ntscf_frame(1);
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_avtp_transport_send(srv, reply_frame.data, reply_frame.len));
    rcp_bytes_free(&reply_frame);

    req_frame = make_ntscf_frame(9);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(cli, req_frame.data, req_frame.len));
    rcp_bytes_free(&req_frame);

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));

    /* srv learned cli's address from that datagram; it can now reply. */
    reply_frame = make_ntscf_frame(2);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(srv, reply_frame.data, reply_frame.len));

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(cli, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(reply_frame.len, out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(reply_frame.data, buf, reply_frame.len);

    rcp_bytes_free(&reply_frame);
    rcp_avtp_transport_release(cli);
    rcp_avtp_transport_release(srv);
}

static void test_recv_times_out_when_empty(void)
{
    rcp_avtp_transport_t *srv;
    rcp_context_t          ctx;
    uint8_t                 buf[256];
    size_t                   out_len = 0;

    srv = bind_or_ignore();
    if (!srv) return;

    ctx = rcp_context_with_timeout_ms(50);
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(srv);
}

static void test_recv_into_too_small_buffer_returns_busy(void)
{
    rcp_avtp_transport_t *srv;
    rcp_avtp_transport_t *cli;
    rcp_context_t          ctx;
    rcp_bytes_t             frame;
    uint8_t                 tiny[1];
    size_t                   out_len = 0;
    uint16_t                 port;

    srv = bind_or_ignore();
    if (!srv) return;
    port = rcp_udp_avtp_transport_port(srv);

    cli = rcp_udp_avtp_transport_dial("127.0.0.1", port, false);
    TEST_ASSERT_TRUE(rcp_udp_avtp_transport_ok(cli));

    frame = make_ntscf_frame(3);
    TEST_ASSERT_TRUE(frame.len > sizeof(tiny));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(cli, frame.data, frame.len));
    rcp_bytes_free(&frame);

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_avtp_transport_recv(srv, &ctx, tiny, sizeof(tiny), &out_len));

    rcp_avtp_transport_release(cli);
    rcp_avtp_transport_release(srv);
}

static void test_send_recv_after_close_returns_closed(void)
{
    rcp_avtp_transport_t *srv;
    rcp_context_t          ctx;
    rcp_bytes_t             frame;
    uint8_t                 buf[256];
    size_t                   out_len = 0;

    srv = bind_or_ignore();
    if (!srv) return;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(srv));

    frame = make_ntscf_frame(4);
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(srv, frame.data, frame.len));
    rcp_bytes_free(&frame);

    ctx = rcp_context_background();
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(srv);
}

typedef struct {
    rcp_avtp_transport_t *t;
    int                     result;
} close_unblock_args_t;

#if defined(_WIN32)
static DWORD WINAPI close_unblock_recv_thread(void *arg)
#else
static void *close_unblock_recv_thread(void *arg)
#endif
{
    close_unblock_args_t *a       = (close_unblock_args_t *)arg;
    rcp_context_t           ctx     = rcp_context_with_timeout_ms(5000);
    uint8_t                 buf[64];
    size_t                   out_len = 0;

    a->result = rcp_avtp_transport_recv(a->t, &ctx, buf, sizeof(buf), &out_len);
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

/* Exercises the file header's own "close() unblocks an in-progress recv()
 * within one poll slice, without ever touching the fd out from under it"
 * contract: a background thread is parked in recv() when close() runs on
 * the same transport from this (the main test) thread. */
static void test_close_unblocks_in_progress_recv(void)
{
    rcp_avtp_transport_t *srv;
    close_unblock_args_t   args;
    test_thread_t           th;
    uint64_t                 start;

    srv = bind_or_ignore();
    if (!srv) return;

    args.t      = srv;
    args.result = RCP_OK;
    th          = test_thread_spawn(close_unblock_recv_thread, &args);

    /* Give the reader thread a moment to actually enter recv() and start
     * polling before close() runs -- a few poll slices' worth is enough. */
    start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < 60) { /* busy-wait */ }

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(srv));
    test_thread_join(th);

    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, args.result);

    rcp_avtp_transport_release(srv);
}

static void test_addr_string_and_port_report_bound_address(void)
{
    rcp_avtp_transport_t *srv;
    char                    buf[64];
    size_t                   n;
    uint16_t                 port;

    srv = bind_or_ignore();
    if (!srv) return;

    port = rcp_udp_avtp_transport_port(srv);
    TEST_ASSERT_TRUE(port > 0);

    n = rcp_udp_avtp_transport_addr_string(srv, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(buf, "127.0.0.1:"));

    rcp_avtp_transport_release(srv);
}

/* REQ-UDP-002: a NULL or empty addr binds INADDR_ANY (0.0.0.0), not a
 * specific interface -- every other bind() test in this file passes
 * "127.0.0.1" explicitly, leaving this branch untested. */
static void test_bind_null_addr_binds_inaddr_any(void)
{
    rcp_avtp_transport_t *srv = rcp_udp_avtp_transport_bind(NULL, 0, false);
    char                    buf[64];
    size_t                   n;

    if (!rcp_udp_avtp_transport_ok(srv)) {
        rcp_avtp_transport_release(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }

    n = rcp_udp_avtp_transport_addr_string(srv, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(buf, "0.0.0.0:"));

    rcp_avtp_transport_release(srv);
}

static void test_bind_empty_addr_binds_inaddr_any(void)
{
    rcp_avtp_transport_t *srv = rcp_udp_avtp_transport_bind("", 0, false);
    char                    buf[64];
    size_t                   n;

    if (!rcp_udp_avtp_transport_ok(srv)) {
        rcp_avtp_transport_release(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }

    n = rcp_udp_avtp_transport_addr_string(srv, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    buf[n] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(buf, "0.0.0.0:"));

    rcp_avtp_transport_release(srv);
}

/* REQ-UDP-003: bind()'s own failure path -- ok() reports false when the
 * underlying bind() call itself fails, not just when socket() fails.
 * Binding the exact same address:port a still-open transport already
 * holds is a portable, reliable way to force a genuine EADDRINUSE. */
static void test_bind_failure_is_not_ok(void)
{
    rcp_avtp_transport_t *first;
    rcp_avtp_transport_t *second;
    uint16_t                 port;

    first = bind_or_ignore();
    if (!first) return;
    port = rcp_udp_avtp_transport_port(first);

    second = rcp_udp_avtp_transport_bind("127.0.0.1", port, false);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_FALSE(rcp_udp_avtp_transport_ok(second));

    rcp_avtp_transport_release(second);
    rcp_avtp_transport_release(first);
}

static void test_dial_unreachable_host_still_ok_at_construct_time(void)
{
    /* connect() on a UDP socket only validates the address family/format,
     * not actual reachability -- this documents that "ok" means "the
     * socket exists and is connect()able", not "a peer is listening". */
    rcp_avtp_transport_t *cli = rcp_udp_avtp_transport_dial("127.0.0.1", 1, true);
    if (!rcp_udp_avtp_transport_ok(cli)) {
        rcp_avtp_transport_release(cli);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    TEST_ASSERT_TRUE(cli->time_sync_supported);
    rcp_avtp_transport_release(cli);
}

static void test_dial_bad_address_is_not_ok(void)
{
    rcp_avtp_transport_t *cli = rcp_udp_avtp_transport_dial("not-an-address", 12345, false);
    TEST_ASSERT_FALSE(rcp_udp_avtp_transport_ok(cli));
    rcp_avtp_transport_release(cli);
}

/* ── Annex J encapsulation sequence number codec: pure, socket-free ──────
 * (rcp_udp_annexj_wrap()/_unwrap(), udp.h's own file header). */

static void test_annexj_wrap_unwrap_roundtrip(void)
{
    static const uint8_t avtpdu[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    rcp_bytes_t            wrapped;
    uint32_t                 seq_out;
    const uint8_t            *payload = NULL;
    size_t                     payload_len = 0;

    wrapped = rcp_udp_annexj_wrap(0x01020304u, avtpdu, sizeof(avtpdu));
    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT(RCP_UDP_ANNEX_J_SEQ_LEN + sizeof(avtpdu), wrapped.len);

    /* Big-endian on the wire, matching this project's own AVTPDU
     * byte-order convention (udp.h's own file header). */
    TEST_ASSERT_EQUAL_HEX8(0x01, wrapped.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, wrapped.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, wrapped.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x04, wrapped.data[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(avtpdu, wrapped.data + RCP_UDP_ANNEX_J_SEQ_LEN, sizeof(avtpdu));

    TEST_ASSERT_TRUE(rcp_udp_annexj_unwrap(wrapped.data, wrapped.len, &seq_out, &payload,
                                            &payload_len));
    TEST_ASSERT_EQUAL_UINT32(0x01020304u, seq_out);
    TEST_ASSERT_EQUAL_UINT(sizeof(avtpdu), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(avtpdu, payload, sizeof(avtpdu));

    rcp_bytes_free(&wrapped);
}

static void test_annexj_wrap_empty_avtpdu(void)
{
    rcp_bytes_t   wrapped = rcp_udp_annexj_wrap(42u, NULL, 0);
    uint32_t        seq_out;
    const uint8_t    *payload     = NULL;
    size_t             payload_len = 1; /* deliberately non-zero: unwrap() must set it to 0 */

    TEST_ASSERT_NOT_NULL(wrapped.data);
    TEST_ASSERT_EQUAL_UINT(RCP_UDP_ANNEX_J_SEQ_LEN, wrapped.len);

    TEST_ASSERT_TRUE(rcp_udp_annexj_unwrap(wrapped.data, wrapped.len, &seq_out, &payload,
                                            &payload_len));
    TEST_ASSERT_EQUAL_UINT32(42u, seq_out);
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&wrapped);
}

static void test_annexj_unwrap_rejects_short_datagram(void)
{
    static const uint8_t too_short[3] = {0x00, 0x00, 0x00};
    uint32_t                seq_out     = 0xFFFFFFFFu;
    const uint8_t             *payload     = (const uint8_t *)1; /* sentinel: must stay untouched */
    size_t                      payload_len = 0xFFu;

    TEST_ASSERT_FALSE(rcp_udp_annexj_unwrap(too_short, sizeof(too_short), &seq_out, &payload,
                                             &payload_len));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, seq_out);
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)1, payload);
    TEST_ASSERT_EQUAL_UINT(0xFFu, payload_len);
}

#if !defined(_WIN32)
/* REQ-UDP-018's own drop-and-keep-waiting clause: a raw datagram shorter
 * than RCP_UDP_ANNEX_J_SEQ_LEN (no room for even the sequence number,
 * let alone an AVTPDU) must not be surfaced to the caller as a
 * malformed/garbage frame -- recv() drops it and keeps waiting for the
 * next one. test_annexj_unwrap_rejects_short_datagram() above only
 * proves the unwrap primitive itself rejects it; this proves recv()'s
 * own integration actually discards it rather than, say, returning an
 * error or (worse) surfacing the too-short bytes as if they were a
 * valid AVTPDU. A raw POSIX socket sends the short datagram directly,
 * bypassing this transport's own send() (which always emits a
 * well-formed Annex J frame) -- Windows has no real UDP transport
 * implementation at all (see this file's own header), so this is
 * POSIX-only, matching bind_or_ignore()'s own platform gating. */
static void test_recv_drops_short_datagram_and_keeps_waiting(void)
{
    rcp_avtp_transport_t *srv;
    rcp_avtp_transport_t *cli;
    rcp_context_t          ctx;
    rcp_bytes_t             good_frame;
    uint8_t                 buf[256];
    size_t                   out_len = 0;
    uint16_t                 port;
    int                       raw_fd;
    struct sockaddr_in       dst;
    static const uint8_t     too_short[3] = {0xAA, 0xBB, 0xCC};

    srv = bind_or_ignore();
    if (!srv) return;
    port = rcp_udp_avtp_transport_port(srv);

    cli = rcp_udp_avtp_transport_dial("127.0.0.1", port, false);
    TEST_ASSERT_TRUE(rcp_udp_avtp_transport_ok(cli));

    raw_fd = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_TRUE(raw_fd >= 0);
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
    (void)sendto(raw_fd, too_short, sizeof(too_short), 0, (struct sockaddr *)&dst, sizeof(dst));
    close(raw_fd);

    /* The malformed datagram alone must not satisfy a recv() -- it is
     * dropped, so a short timeout with nothing else queued still times
     * out rather than returning the garbage bytes. */
    ctx = rcp_context_with_timeout_ms(100);
    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT,
                       rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));

    /* recv() itself is still healthy afterward -- a real, well-formed
     * frame sent right after is received normally. */
    good_frame = make_ntscf_frame(5);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(cli, good_frame.data, good_frame.len));
    rcp_bytes_free(&good_frame);

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(cli);
    rcp_avtp_transport_release(srv);
}
#endif /* !_WIN32 */

/* ── Send-side sequence numbers increment monotonically over a real
 * dial()/bind() pair, and the receiver's own last_recv_seq() reports
 * exactly what was sent (REQ-UDP-017/018). */
static void test_send_seq_increments_and_is_observable_on_receive(void)
{
    rcp_avtp_transport_t *srv;
    rcp_avtp_transport_t *cli;
    rcp_context_t          ctx;
    uint8_t                 buf[256];
    size_t                   out_len;
    int                       i;
    uint16_t                  port;

    srv = bind_or_ignore();
    if (!srv) return;
    port = rcp_udp_avtp_transport_port(srv);

    cli = rcp_udp_avtp_transport_dial("127.0.0.1", port, false);
    TEST_ASSERT_TRUE(rcp_udp_avtp_transport_ok(cli));

    /* Before anything has been received, last_recv_seq() reads 0 --
     * indistinguishable from a real 0 (documented in udp.h), but this is
     * still the documented pre-receive value. */
    TEST_ASSERT_EQUAL_UINT32(0u, rcp_udp_avtp_transport_last_recv_seq(srv));

    for (i = 0; i < 3; i++) {
        rcp_bytes_t frame = make_ntscf_frame((uint8_t)(0x10 + i));
        TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(cli, frame.data, frame.len));
        rcp_bytes_free(&frame);

        ctx = rcp_context_with_timeout_ms(2000);
        out_len = 0;
        TEST_ASSERT_EQUAL(RCP_OK,
                           rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));
        /* cli's own send_seq starts at 0 and increments once per send(). */
        TEST_ASSERT_EQUAL_UINT32((uint32_t)i, rcp_udp_avtp_transport_last_recv_seq(srv));
    }

    rcp_avtp_transport_release(cli);
    rcp_avtp_transport_release(srv);
}

/* ── Default control-port (Annex J, 17221) convenience wrappers
 * (REQ-UDP-019). */
static void test_control_port_constant_is_17221(void)
{
    TEST_ASSERT_EQUAL_UINT16(17221u, RCP_UDP_ANNEX_J_CONTROL_PORT);
    TEST_ASSERT_EQUAL_UINT16(17220u, RCP_UDP_ANNEX_J_CONTINUOUS_PORT);
}

static void test_bind_default_port_binds_control_port(void)
{
    rcp_avtp_transport_t *srv = rcp_udp_avtp_transport_bind_default_port("127.0.0.1", false);
    if (!rcp_udp_avtp_transport_ok(srv)) {
        rcp_avtp_transport_release(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }
    TEST_ASSERT_EQUAL_UINT16(RCP_UDP_ANNEX_J_CONTROL_PORT, rcp_udp_avtp_transport_port(srv));
    rcp_avtp_transport_release(srv);
}

static void test_dial_default_port_targets_control_port(void)
{
    /* Same "connect() only validates address family/format" caveat as
     * test_dial_unreachable_host_still_ok_at_construct_time() above --
     * this only checks that port 17221 was the one actually dialed, via
     * a server bound to that exact port receiving the client's frame. */
    rcp_avtp_transport_t *srv;
    rcp_avtp_transport_t *cli;
    rcp_context_t          ctx;
    rcp_bytes_t             frame;
    uint8_t                 buf[256];
    size_t                   out_len = 0;

    srv = rcp_udp_avtp_transport_bind_default_port("127.0.0.1", false);
    if (!rcp_udp_avtp_transport_ok(srv)) {
        rcp_avtp_transport_release(srv);
        TEST_IGNORE_MESSAGE("UDP transport not available on this platform");
        return;
    }

    cli = rcp_udp_avtp_transport_dial_default_port("127.0.0.1", false);
    TEST_ASSERT_TRUE(rcp_udp_avtp_transport_ok(cli));

    frame = make_ntscf_frame(5);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(cli, frame.data, frame.len));

    ctx = rcp_context_with_timeout_ms(2000);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(srv, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(frame.len, out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame.data, buf, frame.len);

    rcp_bytes_free(&frame);
    rcp_avtp_transport_release(cli);
    rcp_avtp_transport_release(srv);
}

#if defined(_WIN32)
/* REQ-UDP-014: on a platform with no winsock implementation, dial()/
 * bind() still return a non-NULL transport whose ok() reports false and
 * whose send()/recv() return RCP_ERR_CLOSED without crashing -- the
 * exact same "fail cleanly, never NULL, never crash" contract this
 * file's own POSIX tests exercise for the real implementation. This is
 * genuinely only reachable on a build with no winsock implementation
 * (this codebase's own udp.c compiles a full POSIX socket() path on
 * every other platform, guarded by !defined(_WIN32)) -- so this test is
 * itself #if defined(_WIN32)-gated and only ever runs on this repo's own
 * windows-2022 CI job, mirroring how bind_or_ignore() IGNOREs the rest
 * of this file's own POSIX-only tests there instead. */
static void test_win32_stub_dial_and_bind_are_not_ok_and_return_closed(void)
{
    rcp_avtp_transport_t *dialed;
    rcp_avtp_transport_t *bound;
    rcp_context_t          ctx = rcp_context_with_timeout_ms(20);
    uint8_t                 frame[] = {1, 2, 3};
    uint8_t                 buf[16];
    size_t                   out_len = 0;

    dialed = rcp_udp_avtp_transport_dial("127.0.0.1", 12345, false);
    TEST_ASSERT_NOT_NULL(dialed);
    TEST_ASSERT_FALSE(rcp_udp_avtp_transport_ok(dialed));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(dialed, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED,
                       rcp_avtp_transport_recv(dialed, &ctx, buf, sizeof(buf), &out_len));
    rcp_avtp_transport_release(dialed);

    bound = rcp_udp_avtp_transport_bind(NULL, 0, false);
    TEST_ASSERT_NOT_NULL(bound);
    TEST_ASSERT_FALSE(rcp_udp_avtp_transport_ok(bound));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(bound, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED,
                       rcp_avtp_transport_recv(bound, &ctx, buf, sizeof(buf), &out_len));
    rcp_avtp_transport_release(bound);
}
#endif /* _WIN32 */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_dial_send_recv_roundtrip);
    RUN_TEST(test_bind_learns_peer_and_replies);
    RUN_TEST(test_recv_times_out_when_empty);
    RUN_TEST(test_recv_into_too_small_buffer_returns_busy);
    RUN_TEST(test_send_recv_after_close_returns_closed);
    RUN_TEST(test_close_unblocks_in_progress_recv);
    RUN_TEST(test_addr_string_and_port_report_bound_address);
    RUN_TEST(test_bind_null_addr_binds_inaddr_any);
    RUN_TEST(test_bind_empty_addr_binds_inaddr_any);
    RUN_TEST(test_bind_failure_is_not_ok);
    RUN_TEST(test_dial_unreachable_host_still_ok_at_construct_time);
    RUN_TEST(test_dial_bad_address_is_not_ok);

    RUN_TEST(test_annexj_wrap_unwrap_roundtrip);
    RUN_TEST(test_annexj_wrap_empty_avtpdu);
    RUN_TEST(test_annexj_unwrap_rejects_short_datagram);
#if !defined(_WIN32)
    RUN_TEST(test_recv_drops_short_datagram_and_keeps_waiting);
#endif
    RUN_TEST(test_send_seq_increments_and_is_observable_on_receive);
    RUN_TEST(test_control_port_constant_is_17221);
    RUN_TEST(test_bind_default_port_binds_control_port);
    RUN_TEST(test_dial_default_port_targets_control_port);

#if defined(_WIN32)
    RUN_TEST(test_win32_stub_dial_and_bind_are_not_ok_and_return_closed);
#endif

    return UNITY_END();
}
