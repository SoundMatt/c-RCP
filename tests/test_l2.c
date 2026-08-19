/* SPDX-License-Identifier: MPL-2.0 */
/* Frame codec tests for the native-Ethernet AVTPDU transport
 * (rcp_l2_frame_encode()/_decode()): pure byte manipulation, no socket,
 * no privilege, no Linux requirement -- runs everywhere. Construction/
 * transport-level tests below gracefully TEST_IGNORE wherever
 * rcp_l2_avtp_transport_ok() reports the real implementation isn't
 * available (every non-Linux platform's stub, and a Linux build lacking
 * CAP_NET_RAW/root) -- a real send()/recv() round trip over a live veth
 * pair is instead exercised by this project's own Linux-only,
 * elevated-privilege CI job (see ci.yml and l2.h's own file header). */
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/l2.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t k_dst_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
static const uint8_t k_src_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

//cfusa:test REQ-L2-001
static void test_ethertype_constant_value(void)
{
    /* 0x22F0, per the public IEEE 1722-2016 base standard's own subtype/
     * EtherType registry (avtp.h's own file header cites the same
     * standard for its NTSCF/TSCF subtype values). */
    TEST_ASSERT_EQUAL_HEX16(0x22F0u, RCP_L2_ETHERTYPE);
    TEST_ASSERT_EQUAL_UINT(14u, RCP_L2_HEADER_LEN);
}

//cfusa:test REQ-L2-001
static void test_frame_encode_byte_layout(void)
{
    static const uint8_t avtpdu[4] = {0x11, 0x22, 0x33, 0x44};
    rcp_bytes_t             frame;

    frame = rcp_l2_frame_encode(k_dst_mac, k_src_mac, avtpdu, sizeof(avtpdu));
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_L2_HEADER_LEN + sizeof(avtpdu), frame.len);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_dst_mac, frame.data, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_src_mac, frame.data + 6, 6);
    /* EtherType is big-endian on the wire. */
    TEST_ASSERT_EQUAL_HEX8(0x22, frame.data[12]);
    TEST_ASSERT_EQUAL_HEX8(0xF0, frame.data[13]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(avtpdu, frame.data + RCP_L2_HEADER_LEN, sizeof(avtpdu));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-L2-001
//cfusa:test REQ-L2-002
static void test_frame_encode_decode_roundtrip(void)
{
    static const uint8_t avtpdu[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    rcp_bytes_t             frame;
    uint8_t                   out_dst[6];
    uint8_t                    out_src[6];
    const uint8_t                *payload     = NULL;
    size_t                          payload_len = 0;

    frame = rcp_l2_frame_encode(k_dst_mac, k_src_mac, avtpdu, sizeof(avtpdu));
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_TRUE(rcp_l2_frame_decode(frame.data, frame.len, out_dst, out_src, &payload,
                                          &payload_len));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_dst_mac, out_dst, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(k_src_mac, out_src, 6);
    TEST_ASSERT_EQUAL_UINT(sizeof(avtpdu), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(avtpdu, payload, sizeof(avtpdu));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-L2-001
//cfusa:test REQ-L2-002
static void test_frame_encode_empty_avtpdu(void)
{
    rcp_bytes_t frame = rcp_l2_frame_encode(k_dst_mac, k_src_mac, NULL, 0);
    uint8_t       out_dst[6];
    uint8_t        out_src[6];
    const uint8_t    *payload     = NULL;
    size_t              payload_len = 1; /* deliberately non-zero: decode() must set it to 0 */

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_L2_HEADER_LEN, frame.len);

    TEST_ASSERT_TRUE(rcp_l2_frame_decode(frame.data, frame.len, out_dst, out_src, &payload,
                                          &payload_len));
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-L2-002
static void test_frame_decode_rejects_short_frame(void)
{
    static const uint8_t too_short[13] = {0}; /* one octet short of RCP_L2_HEADER_LEN */
    uint8_t                 out_dst[6];
    uint8_t                  out_src[6];
    const uint8_t              *payload     = (const uint8_t *)1; /* sentinel */
    size_t                        payload_len = 0xFFu;

    TEST_ASSERT_FALSE(rcp_l2_frame_decode(too_short, sizeof(too_short), out_dst, out_src,
                                           &payload, &payload_len));
    TEST_ASSERT_EQUAL_PTR((const uint8_t *)1, payload);
    TEST_ASSERT_EQUAL_UINT(0xFFu, payload_len);
}

//cfusa:test REQ-L2-002
static void test_frame_decode_rejects_wrong_ethertype(void)
{
    static const uint8_t avtpdu[2] = {0xAB, 0xCD};
    rcp_bytes_t             frame;
    uint8_t                   out_dst[6];
    uint8_t                    out_src[6];
    const uint8_t                *payload     = NULL;
    size_t                          payload_len = 0;

    frame = rcp_l2_frame_encode(k_dst_mac, k_src_mac, avtpdu, sizeof(avtpdu));
    TEST_ASSERT_NOT_NULL(frame.data);

    /* Corrupt the EtherType field (offsets 12/13) to something that isn't
     * RCP_L2_ETHERTYPE -- e.g. 0x0800 (IPv4). */
    frame.data[12] = 0x08;
    frame.data[13] = 0x00;

    TEST_ASSERT_FALSE(rcp_l2_frame_decode(frame.data, frame.len, out_dst, out_src, &payload,
                                           &payload_len));

    rcp_bytes_free(&frame);
}

/* ── Transport construction: gracefully skips wherever the real Linux
 * implementation isn't available or isn't privileged (see this file's own
 * header comment). A real send()/recv() round trip is this project's own
 * Linux-only, elevated-privilege CI job's job, not this cross-platform
 * unit test's. */
/* REQ-LIFECYCLE-027's write-request unicast gate is built directly on
 * this classifier -- the I/G bit is the least-significant bit of the
 * first octet: 0 == unicast, 1 == multicast (broadcast is the all-ones
 * special case of multicast). */
//cfusa:test REQ-L2-011
static void test_mac_is_unicast_classifies_unicast_multicast_broadcast(void)
{
    static const uint8_t unicast[6]   = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}; /* locally
                                                                                  administered
                                                                                  unicast */
    static const uint8_t multicast[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01}; /* IPv4
                                                                                  multicast
                                                                                  range */
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t all_zero[6]  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; /* I/G bit
                                                                                  clear too */

    TEST_ASSERT_TRUE(rcp_l2_mac_is_unicast(unicast));
    TEST_ASSERT_FALSE(rcp_l2_mac_is_unicast(multicast));
    TEST_ASSERT_FALSE(rcp_l2_mac_is_unicast(broadcast));
    TEST_ASSERT_TRUE(rcp_l2_mac_is_unicast(all_zero));
}

//cfusa:test REQ-L2-003
//cfusa:test REQ-L2-004
//cfusa:test REQ-L2-005
//cfusa:test REQ-L2-010
static void test_transport_new_ok_or_gracefully_unavailable(void)
{
    rcp_avtp_transport_t *t = rcp_l2_avtp_transport_new("lo", k_dst_mac, false);
    uint8_t                 mac[6];

    TEST_ASSERT_NOT_NULL(t);
    if (!rcp_l2_avtp_transport_ok(t)) {
        /* Non-Linux stub, or Linux without CAP_NET_RAW/root -- both are
         * "cleanly reports not ok(), never crashes" per this module's own
         * documented contract, exactly like udp.c's own Windows stub. */
        TEST_ASSERT_FALSE(rcp_l2_avtp_transport_local_mac(t, mac));
        rcp_avtp_transport_release(t);
        TEST_IGNORE_MESSAGE("L2 transport not available/privileged on this platform");
        return;
    }

    TEST_ASSERT_TRUE(rcp_l2_avtp_transport_local_mac(t, mac));
    TEST_ASSERT_EQUAL(false, t->time_sync_supported);

    rcp_avtp_transport_release(t);
}

//cfusa:test REQ-L2-003
//cfusa:test REQ-L2-004
static void test_transport_new_bad_interface_is_not_ok(void)
{
    rcp_avtp_transport_t *t = rcp_l2_avtp_transport_new("no-such-if-9999", k_dst_mac, false);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_FALSE(rcp_l2_avtp_transport_ok(t));
    rcp_avtp_transport_release(t);
}

#if !defined(__linux__)
/* REQ-L2-009 is specifically scoped to "on a non-Linux platform": the
 * stub vtable (src/l2.c's #else branch) unconditionally returns
 * RCP_ERR_CLOSED from send()/recv(), regardless of ok(). This does NOT
 * generalize to an unprivileged *Linux* build with ok()==false --
 * confirmed the hard way: CI's own ubuntu runners have ok()==false here
 * (no CAP_NET_RAW) but still compile the REAL l2_avtp_send()/recv()
 * (src/l2.c's #if defined(__linux__) branch), which don't gate on ok()
 * at all and can return other codes entirely (recv() timed out rather
 * than reporting closed, in CI's own actual run) -- that not-ok()
 * Linux case is exactly what
 * test_transport_new_ok_or_gracefully_unavailable() above already
 * IGNOREs rather than asserting anything about. So this test is itself
 * #if !defined(__linux__)-gated -- it only compiles/runs on this
 * repo's own macOS and Windows CI jobs, where the stub's
 * "ok()==false implies RCP_ERR_CLOSED" contract is unconditionally
 * true (no privilege check needed at all -- see l2_stub_send/recv). */
//cfusa:test REQ-L2-009
static void test_transport_send_recv_closed_when_not_ok(void)
{
    rcp_avtp_transport_t *t = rcp_l2_avtp_transport_new("lo", k_dst_mac, false);
    rcp_context_t          ctx = rcp_context_with_timeout_ms(20);
    uint8_t                frame[] = {1, 2, 3};
    uint8_t                buf[16];
    size_t                  out_len = 0;

    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_FALSE(rcp_l2_avtp_transport_ok(t)); /* the stub is always not-ok */

    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(t, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED,
                       rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(t);
}
#endif /* !__linux__ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ethertype_constant_value);
    RUN_TEST(test_frame_encode_byte_layout);
    RUN_TEST(test_frame_encode_decode_roundtrip);
    RUN_TEST(test_frame_encode_empty_avtpdu);
    RUN_TEST(test_frame_decode_rejects_short_frame);
    RUN_TEST(test_frame_decode_rejects_wrong_ethertype);
    RUN_TEST(test_mac_is_unicast_classifies_unicast_multicast_broadcast);
    RUN_TEST(test_transport_new_ok_or_gracefully_unavailable);
    RUN_TEST(test_transport_new_bad_interface_is_not_ok);
#if !defined(__linux__)
    RUN_TEST(test_transport_send_recv_closed_when_not_ok);
#endif

    return UNITY_END();
}
