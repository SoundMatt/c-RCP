/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-ACF-001
//cfusa:test REQ-ACF-002
//cfusa:test REQ-ACF-003
//cfusa:test REQ-ACF-004
//cfusa:test REQ-ACF-005
//cfusa:test REQ-ACF-006
//cfusa:test REQ-ACF-007
//cfusa:test REQ-ACF-008
//cfusa:test REQ-ACF-009
//cfusa:test REQ-ACF-010
//cfusa:test REQ-ACF-011
//cfusa:test REQ-ACF-012
//cfusa:test REQ-ACF-013
//cfusa:test REQ-ACF-014
//cfusa:test REQ-ACF-015
//cfusa:test REQ-ACF-016
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Message type constants ────────────────────────────────────────────────── */

static void test_msg_type_constants(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0E, RCP_ACF_MSG_TYPE_ABB);
    TEST_ASSERT_EQUAL_HEX8(0x0D, RCP_ACF_MSG_TYPE_GBB);
}

static void test_gbb_header_is_exactly_8_bytes_longer_than_abb(void)
{
    /* The presence/absence of message_timestamp is the only structural
     * difference between the two variants. */
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN + 8, RCP_ACF_GBB_HEADER_LEN);
}

/* ── Golden vectors: TC18 v0.5.1_RC Figure 19 (ACF_ABB) / Figure 20 (ACF_GBB) ─
 *
 * Figure 19: a single ACF_ABB message protected by a trailing CRC32 safe
 * point -- 8-byte header + 6-byte payload + 2 pad octets + 4-byte CRC32 =
 * 20 octets = 5 quadlets, so the spec's own worked example gives
 * acf_msg_length = 0x05. rcp_acf_encode_abb() itself only ever sees the
 * header+payload+pad region (the CRC32 trailer is e2e.c's layer, added
 * afterward -- see acf.h's file header), so what this test pins is the
 * pre-CRC quadlet count acf_msg_length must carry for e2e.c's own +1
 * adaptation to land on the spec's 0x05: 8-byte header + 6-byte payload +
 * 2 pad = 16 octets = 4 quadlets.
 *
 * Figure 20: a single ACF_GBB message, same CRC32 protection -- 8-byte
 * header + 8-byte message_timestamp + 7-byte payload + 1 pad octet +
 * 4-byte CRC32 = 28 octets = 7 quadlets (acf_msg_length = 0x07 per the
 * spec's worked example). Pre-CRC: 16-byte header+timestamp + 7-byte
 * payload + 1 pad = 24 octets = 6 quadlets.
 *
 * See tests/test_e2e.c for the CRC-inclusive +1 adaptation these two
 * pre-CRC figures feed into, and this session's PR description for the
 * actual encoded bytes these two tests produce. */

static void test_golden_figure19_abb_prelcrc_quadlets_and_pad(void)
{
    uint8_t                      body[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    rcp_acf_byte_message_info_t  hdr     = {0};
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  out     = {0};
    const uint8_t               *payload = NULL;
    size_t                        payload_len = 0;

    hdr.byte_bus_id     = 0x03;
    hdr.transaction_num = 0x09;
    hdr.op              = RCP_ACF_OP_WRITE;

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    /* 8 (header) + 6 (payload) + 2 (pad) = 16 octets = 4 quadlets. */
    TEST_ASSERT_EQUAL_UINT(16, frame.len);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[14]); /* pad octet 1 (trails the payload) */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[15]);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, out.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT16(4, out.acf_msg_length); /* pre-CRC quadlet count */
    TEST_ASSERT_EQUAL_UINT8(2, out.pad);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    /* acf_msg_type packed into octet 0 bits 7:1, quadlet-count MSB in
     * bit 0: (0x0E << 1) | 0 = 0x1C. */
    TEST_ASSERT_EQUAL_HEX8(0x1C, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame.data[1]); /* quadlets low 8 bits */

    rcp_bytes_free(&frame);
}

static void test_golden_figure20_gbb_prelcrc_quadlets_and_pad(void)
{
    uint8_t                body[7] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    rcp_acf_gbb_header_t   hdr     = {0};
    rcp_bytes_t            frame;
    rcp_acf_gbb_header_t   out     = {0};
    const uint8_t         *payload = NULL;
    size_t                  payload_len = 0;

    hdr.info.byte_bus_id     = 0x01;
    hdr.info.mtv              = RCP_ACF_MTV_VALID;
    hdr.info.transaction_num  = 0x22;
    hdr.message_timestamp     = 0x0102030405060708ULL;

    frame = rcp_acf_encode_gbb(&hdr, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);

    /* 16 (header+timestamp) + 7 (payload) + 1 (pad) = 24 octets = 6
     * quadlets. */
    TEST_ASSERT_EQUAL_UINT(24, frame.len);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[23]); /* the single pad octet */

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, out.info.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT16(6, out.info.acf_msg_length); /* pre-CRC quadlet count */
    TEST_ASSERT_EQUAL_UINT8(1, out.info.pad);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_EQUAL_UINT64(hdr.message_timestamp, out.message_timestamp);

    /* acf_msg_type packed into octet 0 bits 7:1, quadlet-count MSB in
     * bit 0: (0x0D << 1) | 0 = 0x1A. */
    TEST_ASSERT_EQUAL_HEX8(0x1A, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x06, frame.data[1]); /* quadlets low 8 bits */

    rcp_bytes_free(&frame);
}

/* ── byte_message_info bit-position pins (Table 4) ─────────────────────────── */

static void test_pack_header_bit_positions(void)
{
    uint8_t                       out[8];
    rcp_acf_byte_message_info_t   hdr = {0};

    hdr.pad                       = 0x2u; /* 0b10 */
    hdr.mtv                       = RCP_ACF_MTV_VALID; /* 1 */
    hdr.byte_bus_id                = 0xABu;
    hdr.evt                        = 0x5u; /* 0b0101 */
    hdr.hs                         = 1u;
    hdr.cs                         = 0u;
    hdr.transaction_num            = 0x77u;
    hdr.op                         = RCP_ACF_OP_READ; /* wire op bit = 0 */
    hdr.rsp                        = 1u;
    hdr.err                        = 0u;
    hdr.ms                         = 1u;
    hdr.read_size_or_segment_num   = 0x0ABu; /* 0b0000_1010_1011 */

    /* acf_msg_type = 0x0E, acf_msg_length = 0x145 (0b1_0100_0101). */
    rcp_acf_pack_header(out, RCP_ACF_MSG_TYPE_ABB, 0x145u, &hdr);

    TEST_ASSERT_EQUAL_HEX8((uint8_t)((0x0E << 1) | 0x1u), out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x45u, out[1]);
    /* pad[7:6]=10, mtv[5]=1, rsv[4:3]=00, busid[10:8]=000 -> 1010_0000 */
    TEST_ASSERT_EQUAL_HEX8(0xA0u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xABu, out[3]);
    /* evt[7:4]=0101, rsv[3:2]=00, hs[1]=1, cs[0]=0 -> 0101_0010 */
    TEST_ASSERT_EQUAL_HEX8(0x52u, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x77u, out[5]);
    /* op[7]=0, rsp[6]=1, err[5]=0, ms[4]=1, read_size[11:8]=0000 -> 0101_0000 */
    TEST_ASSERT_EQUAL_HEX8(0x50u, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0xABu, out[7]);
}

static void test_unpack_header_is_pack_header_inverse(void)
{
    uint8_t                       raw[8];
    rcp_acf_byte_message_info_t   hdr = {0};
    rcp_acf_byte_message_info_t   out = {0};

    hdr.pad                      = 0x3u;
    hdr.mtv                      = RCP_ACF_MTV_UNTIMED;
    hdr.byte_bus_id               = 0x5Cu;
    hdr.evt                       = 0x9u;
    hdr.hs                        = 0u;
    hdr.cs                        = 1u;
    hdr.transaction_num           = 0x40u;
    hdr.op                        = RCP_ACF_OP_WRITE;
    hdr.rsp                       = 0u;
    hdr.err                       = 1u;
    hdr.ms                        = 0u;
    hdr.read_size_or_segment_num  = 0x0FEu;

    rcp_acf_pack_header(raw, RCP_ACF_MSG_TYPE_GBB, 0x1FFu, &hdr);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(raw, &out));

    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, out.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT16(0x1FFu, out.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT8(hdr.pad, out.pad);
    TEST_ASSERT_EQUAL_UINT8(hdr.mtv, out.mtv);
    TEST_ASSERT_EQUAL_UINT8(hdr.byte_bus_id, out.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(hdr.evt, out.evt);
    TEST_ASSERT_EQUAL_UINT8(hdr.hs, out.hs);
    TEST_ASSERT_EQUAL_UINT8(hdr.cs, out.cs);
    TEST_ASSERT_EQUAL_UINT8(hdr.transaction_num, out.transaction_num);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_WRITE, out.op); /* WRITE round-trips exactly */
    TEST_ASSERT_EQUAL_UINT8(hdr.rsp, out.rsp);
    TEST_ASSERT_EQUAL_UINT8(hdr.err, out.err);
    TEST_ASSERT_EQUAL_UINT8(hdr.ms, out.ms);
    TEST_ASSERT_EQUAL_UINT16(hdr.read_size_or_segment_num, out.read_size_or_segment_num);
}

static void test_unpack_header_rejects_bus_id_overflow(void)
{
    uint8_t                       raw[8] = {0};
    rcp_acf_byte_message_info_t   out    = {0};

    raw[2] = 0x01u; /* busid[10:8] = 001, nonzero -> exceeds 8-bit rcp_byte_bus_id_t */

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_BUS_ID_OVERFLOW, rcp_acf_unpack_header(raw, &out));
}

static void test_pad_len(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, rcp_acf_pad_len(8));
    TEST_ASSERT_EQUAL_UINT8(3, rcp_acf_pad_len(9));
    TEST_ASSERT_EQUAL_UINT8(2, rcp_acf_pad_len(10));
    TEST_ASSERT_EQUAL_UINT8(1, rcp_acf_pad_len(11));
    TEST_ASSERT_EQUAL_UINT8(0, rcp_acf_pad_len(12));
}

/* ── op wire-bit asymmetry (RCP_ACF_OP_NONE encodes as write) ──────────────── */

static void test_op_none_wire_bit_is_write_roundtrip(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  out = {0};
    const uint8_t                *payload = NULL;
    size_t                         payload_len = 0;

    hdr.op = RCP_ACF_OP_NONE;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE((frame.data[6] & 0x80u) != 0); /* op wire bit = 1 */

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_WRITE, out.op);

    rcp_bytes_free(&frame);
}

static void test_op_read_wire_bit_is_read_roundtrip(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  out = {0};
    const uint8_t                *payload = NULL;
    size_t                         payload_len = 0;

    hdr.op = RCP_ACF_OP_READ;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE((frame.data[6] & 0x80u) == 0); /* op wire bit = 0 */

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_READ, out.op);

    rcp_bytes_free(&frame);
}

/* ── ACF_ABB round-trip ─────────────────────────────────────────────────────── */

static void test_abb_roundtrip_no_payload(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.byte_bus_id    = 5;
    hdr.transaction_num = 0x77;
    hdr.op              = RCP_ACF_OP_WRITE;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, frame.data[0] >> 1);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, out.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT16(RCP_ACF_ABB_HEADER_LEN / 4u, out.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT8(hdr.byte_bus_id, out.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(hdr.transaction_num, out.transaction_num);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_WRITE, out.op);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MTV_UNTIMED, out.mtv);
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_abb_roundtrip_with_payload_and_all_header_fields(void)
{
    uint8_t body[] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.byte_bus_id               = 9;
    hdr.hs                        = 1;
    hdr.cs                        = 1;
    hdr.rsp                       = 1;
    hdr.err                       = 0;
    hdr.ms                        = 1;
    hdr.evt                       = 0xA;
    hdr.op                        = RCP_ACF_OP_READ;
    hdr.transaction_num           = 0x21;
    hdr.read_size_or_segment_num  = 0x0ABC & 0x0FFF; /* exercise the full 12-bit field */

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));
    /* 8 (header) + 4 (body) = 12, already a quadlet multiple -> no pad. */
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN + sizeof(body), frame.len);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16((RCP_ACF_ABB_HEADER_LEN + sizeof(body)) / 4u, out.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    TEST_ASSERT_EQUAL_UINT8(hdr.byte_bus_id, out.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(0, out.pad);
    TEST_ASSERT_EQUAL_UINT8(hdr.hs, out.hs);
    TEST_ASSERT_EQUAL_UINT8(hdr.cs, out.cs);
    TEST_ASSERT_EQUAL_UINT8(hdr.rsp, out.rsp);
    TEST_ASSERT_EQUAL_UINT8(hdr.err, out.err);
    TEST_ASSERT_EQUAL_UINT8(hdr.ms, out.ms);
    TEST_ASSERT_EQUAL_UINT8(hdr.evt, out.evt);
    TEST_ASSERT_EQUAL_UINT8(hdr.op, out.op);
    TEST_ASSERT_EQUAL_UINT8(hdr.transaction_num, out.transaction_num);
    TEST_ASSERT_EQUAL_UINT16(hdr.read_size_or_segment_num, out.read_size_or_segment_num);

    rcp_bytes_free(&frame);
}

static void test_abb_roundtrip_pads_to_quadlet_boundary(void)
{
    uint8_t body[3] = {1, 2, 3}; /* 8 + 3 = 11 -> needs 1 pad octet */
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));
    TEST_ASSERT_EQUAL_UINT(12, frame.len); /* rounded up to 3 quadlets */
    TEST_ASSERT_EQUAL_HEX8(0x00, frame.data[11]); /* the pad octet itself */

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(1, out.pad);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len); /* pad excluded from payload_len */
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));

    rcp_bytes_free(&frame);
}

static void test_abb_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_ACF_ABB_HEADER_LEN - 1] = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_abb(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_abb_decode_rejects_wrong_msg_type(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    frame.data[0] = (uint8_t)(RCP_ACF_MSG_TYPE_GBB << 1);

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_BAD_MSG_TYPE,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_abb_decode_rejects_declared_length_past_buffer(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3};

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_abb(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_abb_encode_rejects_oversized_payload(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_abb(&hdr, NULL, RCP_ACF_ABB_MAX_PAYLOAD + 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(0, frame.len);
}

static void test_abb_encode_accepts_max_payload(void)
{
    static uint8_t body[RCP_ACF_ABB_MAX_PAYLOAD];
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame;

    memset(body, 0x5A, sizeof(body));
    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_ABB_HEADER_LEN + RCP_ACF_ABB_MAX_PAYLOAD, frame.len);

    rcp_bytes_free(&frame);
}

/* ── ACF_GBB round-trip ─────────────────────────────────────────────────────── */

static void test_gbb_roundtrip_no_payload(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.byte_bus_id = 2;
    hdr.info.mtv         = RCP_ACF_MTV_VALID;
    hdr.message_timestamp = 0x0102030405060708ULL;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(RCP_ACF_GBB_HEADER_LEN, frame.len);
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, frame.data[0] >> 1);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, out.info.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT8(hdr.info.byte_bus_id, out.info.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MTV_VALID, out.info.mtv);
    TEST_ASSERT_EQUAL_UINT64(hdr.message_timestamp, out.message_timestamp);
    TEST_ASSERT_EQUAL_UINT(0, payload_len);

    rcp_bytes_free(&frame);
}

static void test_gbb_roundtrip_with_payload(void)
{
    uint8_t body[] = {0x01, 0x02};
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.byte_bus_id = 1;
    hdr.info.mtv         = RCP_ACF_MTV_VALID;
    hdr.info.op          = RCP_ACF_OP_READ;
    hdr.message_timestamp = 0xAABBCCDDEEFF0011ULL;

    frame = rcp_acf_encode_gbb(&hdr, body, sizeof(body));
    /* 16 (header+ts) + 2 (body) = 18 -> pad to 20 (5 quadlets). */
    TEST_ASSERT_EQUAL_UINT(20, frame.len);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(5, out.info.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT8(2, out.info.pad);
    TEST_ASSERT_EQUAL_UINT(sizeof(body), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, payload, sizeof(body));
    TEST_ASSERT_EQUAL_UINT64(hdr.message_timestamp, out.message_timestamp);

    rcp_bytes_free(&frame);
}

static void test_gbb_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_ACF_GBB_HEADER_LEN - 1] = {0};
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_gbb(buf, sizeof(buf), &out, &payload, &payload_len));
}

static void test_gbb_decode_rejects_wrong_msg_type(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    frame.data[0] = (uint8_t)(RCP_ACF_MSG_TYPE_ABB << 1);

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_BAD_MSG_TYPE,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_gbb_decode_rejects_declared_length_past_buffer(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t body[] = {1, 2, 3, 4};

    frame = rcp_acf_encode_gbb(&hdr, body, sizeof(body));

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_gbb(frame.data, frame.len - 1, &out, &payload, &payload_len));

    rcp_bytes_free(&frame);
}

static void test_gbb_encode_rejects_oversized_payload(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_gbb(&hdr, NULL, RCP_ACF_GBB_MAX_PAYLOAD + 1);

    TEST_ASSERT_NULL(frame.data);
    TEST_ASSERT_EQUAL_UINT(0, frame.len);
}

/* ── Timestamp-validity folding ────────────────────────────────────────────── */

static void test_gbb_is_timed_true_only_when_valid(void)
{
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_VALID;
    TEST_ASSERT_TRUE(rcp_acf_gbb_is_timed(&hdr));
}

static void test_gbb_is_timed_false_when_untimed(void)
{
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_UNTIMED;
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&hdr));
}

static void test_gbb_encode_zeroes_timestamp_region_when_untimed(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.mtv          = RCP_ACF_MTV_UNTIMED;
    hdr.message_timestamp = 0xFFFFFFFFFFFFFFFFULL; /* nonzero on purpose */

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));

    TEST_ASSERT_EQUAL_UINT64(0, out.message_timestamp);
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&out));

    rcp_bytes_free(&frame);
}

static void test_gbb_encode_preserves_timestamp_when_valid(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    hdr.info.mtv          = RCP_ACF_MTV_VALID;
    hdr.message_timestamp = 0x42;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));

    TEST_ASSERT_EQUAL_UINT64(0x42, out.message_timestamp);
    TEST_ASSERT_TRUE(rcp_acf_gbb_is_timed(&out));

    rcp_bytes_free(&frame);
}

/* ── Four response-type identification rules ───────────────────────────────── */

static void test_classify_response_error_takes_priority(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.err = 1;
    hdr.op  = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
}

static void test_classify_response_write(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_WRITE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_WRITE, rcp_acf_classify_response(&hdr));
}

static void test_classify_response_read(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_READ, rcp_acf_classify_response(&hdr));
}

static void test_classify_response_acknowledge(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_NONE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
}

static void test_ack_has_event_true_when_evt_nonzero(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op  = RCP_ACF_OP_NONE;
    hdr.evt = 3;
    TEST_ASSERT_TRUE(rcp_acf_hdr_ack_has_event(&hdr));
}

static void test_ack_has_event_false_when_evt_zero(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op  = RCP_ACF_OP_NONE;
    hdr.evt = 0;
    TEST_ASSERT_FALSE(rcp_acf_hdr_ack_has_event(&hdr));
}

static void test_ack_has_event_false_when_not_an_acknowledge(void)
{
    /* evt is only meaningful for classifying an Acknowledge; a Write or
     * Read response with a nonzero evt is not "an acknowledge with an
     * event" by this rule. */
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op  = RCP_ACF_OP_WRITE;
    hdr.evt = 7;
    TEST_ASSERT_FALSE(rcp_acf_hdr_ack_has_event(&hdr));
}

/* ── Message-type dispatch ─────────────────────────────────────────────────── */

static void test_peek_msg_type_reads_first_byte(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    uint8_t msg_type = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(frame.data, frame.len, &msg_type));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, msg_type);

    rcp_bytes_free(&frame);
}

static void test_peek_msg_type_rejects_empty_buffer(void)
{
    uint8_t msg_type = 0;
    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME, rcp_acf_peek_msg_type(NULL, 0, &msg_type));
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

static void test_acf_strerror_unique_nonempty(void)
{
    const rcp_acf_errc_t codes[] = {
        RCP_ACF_OK, RCP_ACF_ERR_SHORT_FRAME, RCP_ACF_ERR_BAD_MSG_TYPE,
        RCP_ACF_ERR_BUS_ID_OVERFLOW,
    };
    const size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_acf_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_acf_strerror(codes[j])) != 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_msg_type_constants);
    RUN_TEST(test_gbb_header_is_exactly_8_bytes_longer_than_abb);

    RUN_TEST(test_golden_figure19_abb_prelcrc_quadlets_and_pad);
    RUN_TEST(test_golden_figure20_gbb_prelcrc_quadlets_and_pad);

    RUN_TEST(test_pack_header_bit_positions);
    RUN_TEST(test_unpack_header_is_pack_header_inverse);
    RUN_TEST(test_unpack_header_rejects_bus_id_overflow);
    RUN_TEST(test_pad_len);

    RUN_TEST(test_op_none_wire_bit_is_write_roundtrip);
    RUN_TEST(test_op_read_wire_bit_is_read_roundtrip);

    RUN_TEST(test_abb_roundtrip_no_payload);
    RUN_TEST(test_abb_roundtrip_with_payload_and_all_header_fields);
    RUN_TEST(test_abb_roundtrip_pads_to_quadlet_boundary);
    RUN_TEST(test_abb_decode_rejects_short_frame);
    RUN_TEST(test_abb_decode_rejects_wrong_msg_type);
    RUN_TEST(test_abb_decode_rejects_declared_length_past_buffer);
    RUN_TEST(test_abb_encode_rejects_oversized_payload);
    RUN_TEST(test_abb_encode_accepts_max_payload);

    RUN_TEST(test_gbb_roundtrip_no_payload);
    RUN_TEST(test_gbb_roundtrip_with_payload);
    RUN_TEST(test_gbb_decode_rejects_short_frame);
    RUN_TEST(test_gbb_decode_rejects_wrong_msg_type);
    RUN_TEST(test_gbb_decode_rejects_declared_length_past_buffer);
    RUN_TEST(test_gbb_encode_rejects_oversized_payload);

    RUN_TEST(test_gbb_is_timed_true_only_when_valid);
    RUN_TEST(test_gbb_is_timed_false_when_untimed);
    RUN_TEST(test_gbb_encode_zeroes_timestamp_region_when_untimed);
    RUN_TEST(test_gbb_encode_preserves_timestamp_when_valid);

    RUN_TEST(test_classify_response_error_takes_priority);
    RUN_TEST(test_classify_response_write);
    RUN_TEST(test_classify_response_read);
    RUN_TEST(test_classify_response_acknowledge);
    RUN_TEST(test_ack_has_event_true_when_evt_nonzero);
    RUN_TEST(test_ack_has_event_false_when_evt_zero);
    RUN_TEST(test_ack_has_event_false_when_not_an_acknowledge);

    RUN_TEST(test_peek_msg_type_reads_first_byte);
    RUN_TEST(test_peek_msg_type_rejects_empty_buffer);

    RUN_TEST(test_acf_strerror_unique_nonempty);

    return UNITY_END();
}
