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
#include <pthread.h>
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
    RUN_TEST(test_dial_unreachable_host_still_ok_at_construct_time);
    RUN_TEST(test_dial_bad_address_is_not_ok);

    return UNITY_END();
}
