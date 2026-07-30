/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-AVTP-001
//cfusa:test REQ-AVTP-002
//cfusa:test REQ-AVTP-003
//cfusa:test REQ-AVTP-004
//cfusa:test REQ-AVTP-005
//cfusa:test REQ-AVTP-006
//cfusa:test REQ-AVTP-007
//cfusa:test REQ-AVTP-008
//cfusa:test REQ-AVTP-009
//cfusa:test REQ-AVTP-010
//cfusa:test REQ-AVTP-011
//cfusa:test REQ-AVTP-012
//cfusa:test REQ-AVTP-013
//cfusa:test REQ-AVTP-014
//cfusa:test REQ-AVTP-015
//cfusa:test REQ-AVTP-016
//cfusa:test REQ-AVTP-017
//cfusa:test REQ-AVTP-018
//cfusa:test REQ-AVTP-019
//cfusa:test REQ-AVTP-020
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMacA[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t kMacB[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

/* ── Subtype constants ─────────────────────────────────────────────────────── */

static void test_subtype_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x82, RCP_AVTP_SUBTYPE_NTSCF);
    TEST_ASSERT_EQUAL_HEX8(0x05, RCP_AVTP_SUBTYPE_TSCF);
}

/* ── stream_id ─────────────────────────────────────────────────────────────── */

static void test_stream_id_make_preserves_mac_and_unique_id(void)
{
    rcp_stream_id_t id = rcp_stream_id_make(kMacA, 0xBEEF);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(kMacA, id.mac, 6);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, id.unique_id);
}

static void test_stream_id_u64_roundtrip(void)
{
    rcp_stream_id_t id = rcp_stream_id_make(kMacA, 0x1234);
    uint64_t packed = rcp_stream_id_to_u64(id);
    rcp_stream_id_t back = rcp_stream_id_from_u64(packed);

    TEST_ASSERT_TRUE(rcp_stream_id_equal(id, back));
}

static void test_stream_id_u64_matches_expected_layout(void)
{
    /* MAC in the high 48 bits, unique_id in the low 16 bits. */
    rcp_stream_id_t id = rcp_stream_id_make(kMacA, 0x0102);
    uint64_t packed = rcp_stream_id_to_u64(id);
    uint64_t expected = ((uint64_t)0x02 << 40) | ((uint64_t)0x11 << 32) |
                         ((uint64_t)0x22 << 24) | ((uint64_t)0x33 << 16) |
                         ((uint64_t)0x44 << 8)  |  (uint64_t)0x55;
    expected = (expected << 16) | 0x0102u;

    TEST_ASSERT_EQUAL_UINT64(expected, packed);
}

static void test_stream_id_equal_requires_mac_and_unique_id_match(void)
{
    rcp_stream_id_t a = rcp_stream_id_make(kMacA, 1);
    rcp_stream_id_t b = rcp_stream_id_make(kMacA, 2);
    rcp_stream_id_t c = rcp_stream_id_make(kMacB, 1);
    rcp_stream_id_t d = rcp_stream_id_make(kMacA, 1);

    TEST_ASSERT_FALSE(rcp_stream_id_equal(a, b));
    TEST_ASSERT_FALSE(rcp_stream_id_equal(a, c));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(a, d));
}

/* ── byte_bus_id / addressing ──────────────────────────────────────────────── */

static void test_avtp_addr_equal_requires_byte_bus_id_match(void)
{
    rcp_avtp_addr_t a1, a2, a3;

    a1.stream_id = rcp_stream_id_make(kMacA, 7);
    a1.byte_bus_id = 3;

    a2.stream_id = rcp_stream_id_make(kMacA, 7);
    a2.byte_bus_id = 4; /* same stream_id, different endpoint */

    a3.stream_id = rcp_stream_id_make(kMacA, 7);
    a3.byte_bus_id = 3;

    TEST_ASSERT_FALSE(rcp_avtp_addr_equal(a1, a2));
    TEST_ASSERT_TRUE(rcp_avtp_addr_equal(a1, a3));
}

/* ── NTSCF round-trip ──────────────────────────────────────────────────────── */

static void test_ntscf_roundtrip_no_payload(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.version = 0;
    hdr.sequence_num = 0x42;
    hdr.stream_id = rcp_stream_id_make(kMacA, 0x0001);

    frame = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_NTSCF, frame.data[0]);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(hdr.sv, out.sv);
    TEST_ASSERT_EQUAL_UINT8(hdr.version, out.version);
    TEST_ASSERT_EQUAL_UINT8(hdr.sequence_num, out.sequence_num);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));
    TEST_ASSERT_EQUAL_UINT(0, payload_len);
    TEST_ASSERT_EQUAL_UINT16(0, out.ntscf_data_length);

    rcp_bytes_free(&frame);
}

static void test_ntscf_roundtrip_with_payload(void)
{
    uint8_t body[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.sequence_num = 7;
    hdr.stream_id = rcp_stream_id_make(kMacB, 0x9988);

    frame = rcp_avtp_encode_ntscf(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), out.ntscf_data_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));

    rcp_bytes_free(&frame);
}

static void test_ntscf_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_AVTP_NTSCF_HEADER_LEN - 1] = {0};
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_ntscf(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_ntscf_decode_rejects_wrong_subtype(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    frame.data[0] = RCP_AVTP_SUBTYPE_TSCF;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_BAD_SUBTYPE,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_ntscf_decode_rejects_declared_length_past_buffer(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3};

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, body, sizeof(body));

    /* Claim more payload than the buffer actually holds by decoding with a
     * truncated length, while the data-length field still says 3. */
    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_ntscf(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_ntscf_encode_rejects_oversized_payload(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, NULL, RCP_AVTP_NTSCF_MAX_PAYLOAD + 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(0, frame.len);
}

/* ── TSCF round-trip ───────────────────────────────────────────────────────── */

static void test_tscf_roundtrip_no_payload(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.tv = 1;
    hdr.tu = 0;
    hdr.mr = 1;
    hdr.sequence_num = 0x99;
    hdr.stream_id = rcp_stream_id_make(kMacA, 0x0002);
    hdr.avtp_timestamp = 0x12345678u;

    frame = rcp_avtp_encode_tscf(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_TSCF_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_TSCF, frame.data[0]);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_tscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(hdr.sv, out.sv);
    TEST_ASSERT_EQUAL_UINT8(hdr.mr, out.mr);
    TEST_ASSERT_EQUAL_UINT8(hdr.tv, out.tv);
    TEST_ASSERT_EQUAL_UINT8(hdr.tu, out.tu);
    TEST_ASSERT_EQUAL_UINT8(hdr.sequence_num, out.sequence_num);
    TEST_ASSERT_EQUAL_UINT32(hdr.avtp_timestamp, out.avtp_timestamp);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_tscf_roundtrip_with_payload(void)
{
    uint8_t body[] = {0xAA, 0xBB, 0xCC};
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.sv = 1;
    hdr.tu = 1;
    hdr.stream_id = rcp_stream_id_make(kMacB, 0x4321);
    hdr.avtp_timestamp = 42;

    frame = rcp_avtp_encode_tscf(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_TSCF_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_tscf(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(sizeof(body), out.stream_data_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_EQUAL_UINT8(1, out.tu);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(hdr.stream_id, out.stream_id));

    rcp_bytes_free(&frame);
}

static void test_tscf_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_AVTP_TSCF_HEADER_LEN - 1] = {0};
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_tscf(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_tscf_decode_rejects_wrong_subtype(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_tscf(&hdr, NULL, 0);
    frame.data[0] = RCP_AVTP_SUBTYPE_NTSCF;

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_BAD_SUBTYPE,
                       rcp_avtp_decode_tscf(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_tscf_decode_rejects_declared_length_past_buffer(void)
{
    rcp_avtp_tscf_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_tscf_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3, 4};

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_tscf(&hdr, body, sizeof(body));

    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME,
                       rcp_avtp_decode_tscf(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

/* ── Subtype dispatch & TSCF drop rule ─────────────────────────────────────── */

static void test_peek_subtype_reads_first_byte(void)
{
    rcp_avtp_ntscf_header_t hdr = {0};
    rcp_bytes_t frame;
    uint8_t subtype = 0;

    hdr.stream_id = rcp_stream_id_make(kMacA, 1);
    frame = rcp_avtp_encode_ntscf(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_peek_subtype(frame.data, frame.len, &subtype));
    TEST_ASSERT_EQUAL_HEX8(RCP_AVTP_SUBTYPE_NTSCF, subtype);

    rcp_bytes_free(&frame);
}

static void test_peek_subtype_rejects_empty_buffer(void)
{
    uint8_t subtype = 0;
    TEST_ASSERT_EQUAL(RCP_AVTP_ERR_SHORT_FRAME, rcp_avtp_peek_subtype(NULL, 0, &subtype));
}

static void test_should_drop_tscf_without_time_sync(void)
{
    TEST_ASSERT_TRUE(rcp_avtp_should_drop_tscf(false, RCP_AVTP_SUBTYPE_TSCF));
}

static void test_should_not_drop_tscf_with_time_sync(void)
{
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(true, RCP_AVTP_SUBTYPE_TSCF));
}

static void test_should_not_drop_ntscf_regardless_of_time_sync(void)
{
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(false, RCP_AVTP_SUBTYPE_NTSCF));
    TEST_ASSERT_FALSE(rcp_avtp_should_drop_tscf(true, RCP_AVTP_SUBTYPE_NTSCF));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_avtp_strerror_unique_nonempty(void)
{
    const rcp_avtp_errc_t codes[] = {
        RCP_AVTP_OK, RCP_AVTP_ERR_SHORT_FRAME, RCP_AVTP_ERR_BAD_SUBTYPE,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_avtp_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_avtp_strerror(codes[j])) != 0);
        }
    }
}

/* ── Loopback transport ────────────────────────────────────────────────────── */

static void test_loopback_transport_send_recv_fifo_order(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 4);
    rcp_context_t ctx = rcp_context_with_timeout_ms(50);
    uint8_t frame_a[] = {1, 2, 3};
    uint8_t frame_b[] = {4, 5};
    uint8_t buf[16];
    size_t out_len = 0;

    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(t->time_sync_supported);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame_a, sizeof(frame_a)));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame_b, sizeof(frame_b)));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame_a), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_a, buf, sizeof(frame_a));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(frame_b), out_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame_b, buf, sizeof(frame_b));

    rcp_avtp_transport_release(t);
}

static void test_loopback_transport_recv_times_out_when_empty(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 2);
    rcp_context_t ctx = rcp_context_with_timeout_ms(20);
    uint8_t buf[8];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(RCP_ERR_TIMEOUT, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(t);
}

static void test_loopback_transport_rejects_after_close(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 2);
    rcp_context_t ctx = rcp_context_with_timeout_ms(20);
    uint8_t frame[] = {9};
    uint8_t buf[8];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_close(t));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_send(t, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_CLOSED, rcp_avtp_transport_recv(t, &ctx, buf, sizeof(buf), &out_len));

    rcp_avtp_transport_release(t);
}

static void test_loopback_transport_send_rejects_when_full(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 1);
    uint8_t frame[] = {1};

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL(RCP_ERR_BUSY, rcp_avtp_transport_send(t, frame, sizeof(frame)));

    rcp_avtp_transport_release(t);
}

static void test_loopback_transport_refcount_defers_destroy(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(true, 1);
    uint8_t frame[] = {1};

    rcp_avtp_transport_retain(t);
    rcp_avtp_transport_release(t); /* back to refcount 1: still usable */

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(t, frame, sizeof(frame)));

    rcp_avtp_transport_release(t); /* drops to 0: frees */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_subtype_constants);

    RUN_TEST(test_stream_id_make_preserves_mac_and_unique_id);
    RUN_TEST(test_stream_id_u64_roundtrip);
    RUN_TEST(test_stream_id_u64_matches_expected_layout);
    RUN_TEST(test_stream_id_equal_requires_mac_and_unique_id_match);

    RUN_TEST(test_avtp_addr_equal_requires_byte_bus_id_match);

    RUN_TEST(test_ntscf_roundtrip_no_payload);
    RUN_TEST(test_ntscf_roundtrip_with_payload);
    RUN_TEST(test_ntscf_decode_rejects_short_frame);
    RUN_TEST(test_ntscf_decode_rejects_wrong_subtype);
    RUN_TEST(test_ntscf_decode_rejects_declared_length_past_buffer);
    RUN_TEST(test_ntscf_encode_rejects_oversized_payload);

    RUN_TEST(test_tscf_roundtrip_no_payload);
    RUN_TEST(test_tscf_roundtrip_with_payload);
    RUN_TEST(test_tscf_decode_rejects_short_frame);
    RUN_TEST(test_tscf_decode_rejects_wrong_subtype);
    RUN_TEST(test_tscf_decode_rejects_declared_length_past_buffer);

    RUN_TEST(test_peek_subtype_reads_first_byte);
    RUN_TEST(test_peek_subtype_rejects_empty_buffer);
    RUN_TEST(test_should_drop_tscf_without_time_sync);
    RUN_TEST(test_should_not_drop_tscf_with_time_sync);
    RUN_TEST(test_should_not_drop_ntscf_regardless_of_time_sync);

    RUN_TEST(test_avtp_strerror_unique_nonempty);

    RUN_TEST(test_loopback_transport_send_recv_fifo_order);
    RUN_TEST(test_loopback_transport_recv_times_out_when_empty);
    RUN_TEST(test_loopback_transport_rejects_after_close);
    RUN_TEST(test_loopback_transport_send_rejects_when_full);
    RUN_TEST(test_loopback_transport_refcount_defers_destroy);

    return UNITY_END();
}
