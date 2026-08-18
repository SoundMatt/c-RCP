/* SPDX-License-Identifier: MPL-2.0 */
/* c-RCP-18-tracker (issue #533): the per-id test-trace tags that used to
 * be stacked here at the file header (satisfying cfusa's --sec-tested
 * gate for every id in the file regardless of which test function, if
 * any, actually exercises each one) have been moved to sit directly
 * above the specific test function that proves each requirement, per
 * CONTRIBUTING.md's "Writing a requirement" convention. See each test
 * function below for its own tag(s). (Deliberately not spelling out the
 * literal tag syntax in this paragraph -- cfusa's own scanner reads that
 * exact character sequence anywhere in a line, comment or not, as a real
 * tag with whatever word follows it as the id, which is exactly the
 * dangling-reference noise this rewrite fixes; see the PR description.) */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Message type constants ────────────────────────────────────────────────── */

//cfusa:test REQ-ACF-017
static void test_msg_type_abb_constant(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0E, RCP_ACF_MSG_TYPE_ABB);
}

//cfusa:test REQ-ACF-048
static void test_msg_type_gbb_constant(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0D, RCP_ACF_MSG_TYPE_GBB);
}

//cfusa:test REQ-ACF-044
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

//cfusa:test REQ-ACF-004
//cfusa:test REQ-ACF-006
//cfusa:test REQ-ACF-014
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

//cfusa:test REQ-ACF-038
//cfusa:test REQ-ACF-040
//cfusa:test REQ-ACF-044
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

//cfusa:test REQ-ACF-016
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

//cfusa:test REQ-ACF-046
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
    TEST_ASSERT_EQUAL_UINT16(hdr.byte_bus_id, out.byte_bus_id);
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

/* Hand-crafted raw octets (independent of rcp_acf_pack_header() -- the
 * same field values test_pack_header_bit_positions() feeds INTO pack, but
 * computed here by hand and fed directly to unpack, so this test proves
 * rcp_acf_unpack_header()'s own bit layout even if pack_header's own logic
 * were wrong): acf_msg_type=0x0E, acf_msg_length=0x145, pad=0b10, mtv=1,
 * byte_bus_id=0xAB, evt=0x5, hs=1, cs=0, transaction_num=0x77, op wire
 * bit=0 (READ), rsp=1, err=0, ms=1, read_size_or_segment_num=0x0AB. */
//cfusa:test REQ-ACF-046
static void test_unpack_header_bit_positions_from_raw_bytes(void)
{
    const uint8_t                raw[8] = {0x1D, 0x45, 0xA0, 0xAB, 0x52, 0x77, 0x50, 0xAB};
    rcp_acf_byte_message_info_t  out    = {0};

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(raw, &out));

    TEST_ASSERT_EQUAL_HEX8(0x0Eu, out.acf_msg_type);
    TEST_ASSERT_EQUAL_UINT16(0x145u, out.acf_msg_length);
    TEST_ASSERT_EQUAL_UINT8(0x2u, out.pad);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MTV_VALID, out.mtv);
    TEST_ASSERT_EQUAL_UINT16(0xABu, out.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(0x5u, out.evt);
    TEST_ASSERT_EQUAL_UINT8(1u, out.hs);
    TEST_ASSERT_EQUAL_UINT8(0u, out.cs);
    TEST_ASSERT_EQUAL_UINT8(0x77u, out.transaction_num);
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_OP_READ, out.op); /* wire op bit = 0 */
    TEST_ASSERT_EQUAL_UINT8(1u, out.rsp);
    TEST_ASSERT_EQUAL_UINT8(0u, out.err);
    TEST_ASSERT_EQUAL_UINT8(1u, out.ms);
    TEST_ASSERT_EQUAL_UINT16(0x0ABu, out.read_size_or_segment_num);
}

/* REQ-RMAP-053/REQ-ACF-020: byte_bus_id is 11 bits wide on the wire
 * (octet 2 bits [2:0] carry [10:8], octet 3 carries [7:0]).
 * rcp_byte_bus_id_t (avtp.h) now holds the full 0-2047 range, so a
 * wire value whose [10:8] bits are nonzero decodes correctly instead
 * of being rejected. */
static void test_unpack_header_reads_full_11_bit_bus_id(void)
{
    uint8_t                       raw[8] = {0};
    rcp_acf_byte_message_info_t   hdr    = {0};
    rcp_acf_byte_message_info_t   out    = {0};

    /* Hand-craft the maximum representable value, 0x7FF (endpoint 2047):
     * octet 2 bits [2:0] = 111, octet 3 = 0xFF. */
    raw[2] = 0x07u;
    raw[3] = 0xFFu;

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(raw, &out));
    TEST_ASSERT_EQUAL_UINT16(0x7FFu, out.byte_bus_id);

    /* Round-trip the same value through pack_header too. */
    hdr.byte_bus_id = 0x7FFu;
    hdr.op          = RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(raw, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL_HEX8(0x07u, (uint8_t)(raw[2] & 0x07u));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, raw[3]);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(raw, &out));
    TEST_ASSERT_EQUAL_UINT16(0x7FFu, out.byte_bus_id);

    /* A mid-range value above the old 8-bit ceiling (256) also
     * round-trips: this is exactly the address space that was
     * previously unreachable. */
    hdr.byte_bus_id = 0x321u; /* 801 */
    rcp_acf_pack_header(raw, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_unpack_header(raw, &out));
    TEST_ASSERT_EQUAL_UINT16(0x321u, out.byte_bus_id);
}

//cfusa:test REQ-ACF-032
/* rcp_acf_peek_gbb_request_type() reads a GBB frame's own
 * request_type (frame offset 8, the message_timestamp region's own
 * repurposed leading octet) without decoding into any specific
 * conditional-request kind's own struct. */
static void test_peek_gbb_request_type(void)
{
    uint8_t                     frame[9] = {0};
    rcp_acf_byte_message_info_t hdr      = {0};
    uint8_t                     request_type = 0xFFu;

    /* A genuine GBB frame: request_type lives at the fixed offset 8. */
    rcp_acf_pack_header(frame, RCP_ACF_MSG_TYPE_GBB, 2u, &hdr);
    frame[8] = 0x0Fu; /* RCP_REQUEST_TYPE_COMPOUND, request_compound.h */
    TEST_ASSERT_TRUE(rcp_acf_peek_gbb_request_type(frame, sizeof(frame), &request_type));
    TEST_ASSERT_EQUAL_HEX8(0x0Fu, request_type);

    /* An ABB frame has no request_type concept at all -- rejected even
     * though byte 8 exists and is nonzero. */
    request_type = 0xFFu;
    rcp_acf_pack_header(frame, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    frame[8] = 0x0Fu;
    TEST_ASSERT_FALSE(rcp_acf_peek_gbb_request_type(frame, sizeof(frame), &request_type));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, request_type); /* left unchanged */

    /* A GBB frame too short to hold byte_message_info(8) + request_type(1)
     * is rejected, not read out of bounds. */
    request_type = 0xFFu;
    rcp_acf_pack_header(frame, RCP_ACF_MSG_TYPE_GBB, 2u, &hdr);
    TEST_ASSERT_FALSE(rcp_acf_peek_gbb_request_type(frame, 8u, &request_type));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, request_type);
}

//cfusa:test REQ-ACF-047
static void test_pad_len(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, rcp_acf_pad_len(8));
    TEST_ASSERT_EQUAL_UINT8(3, rcp_acf_pad_len(9));
    TEST_ASSERT_EQUAL_UINT8(2, rcp_acf_pad_len(10));
    TEST_ASSERT_EQUAL_UINT8(1, rcp_acf_pad_len(11));
    TEST_ASSERT_EQUAL_UINT8(0, rcp_acf_pad_len(12));
}

/* ── op wire-bit asymmetry (RCP_ACF_OP_NONE encodes as write) ──────────────── */

//cfusa:test REQ-ACF-019
//cfusa:test REQ-ACF-049
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

//cfusa:test REQ-ACF-019
//cfusa:test REQ-ACF-049
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

//cfusa:test REQ-ACF-004
//cfusa:test REQ-ACF-014
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

//cfusa:test REQ-ACF-010
//cfusa:test REQ-ACF-015
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

//cfusa:test REQ-ACF-005
static void test_abb_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_ACF_ABB_HEADER_LEN - 1] = {0};
    rcp_acf_byte_message_info_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_abb(buf, sizeof(buf), &out, &payload, &payload_len));
}

//cfusa:test REQ-ACF-007
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

//cfusa:test REQ-ACF-009
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

//cfusa:test REQ-ACF-038
//cfusa:test REQ-ACF-044
//cfusa:test REQ-ACF-043
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

//cfusa:test REQ-ACF-042
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

/* REQ-ACF-045 split off REQ-ACF-015 (c-RCP-18-tracker, issue #533): the
 * GBB half of byte_message_info's remaining flag/count field round-trip
 * -- hs/cs/rsp/err/ms/read_size_or_segment_num -- previously had no test
 * of its own distinct from the ABB half
 * (test_abb_roundtrip_with_payload_and_all_header_fields); this is that
 * dedicated GBB test. */
//cfusa:test REQ-ACF-045
static void test_gbb_roundtrip_remaining_header_fields(void)
{
    rcp_acf_gbb_header_t hdr = {0};
    rcp_bytes_t          frame;
    rcp_acf_gbb_header_t out = {0};
    const uint8_t        *payload = NULL;
    size_t                payload_len = 0;

    hdr.info.byte_bus_id              = 4;
    hdr.info.hs                       = 1;
    hdr.info.cs                       = 1;
    hdr.info.rsp                      = 1;
    hdr.info.err                      = 0;
    hdr.info.ms                       = 1;
    hdr.info.op                       = RCP_ACF_OP_READ;
    hdr.info.read_size_or_segment_num = 0x0CDE & 0x0FFF;

    frame = rcp_acf_encode_gbb(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(hdr.info.hs, out.info.hs);
    TEST_ASSERT_EQUAL_UINT8(hdr.info.cs, out.info.cs);
    TEST_ASSERT_EQUAL_UINT8(hdr.info.rsp, out.info.rsp);
    TEST_ASSERT_EQUAL_UINT8(hdr.info.err, out.info.err);
    TEST_ASSERT_EQUAL_UINT8(hdr.info.ms, out.info.ms);
    TEST_ASSERT_EQUAL_UINT16(hdr.info.read_size_or_segment_num, out.info.read_size_or_segment_num);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-ACF-039
static void test_gbb_decode_rejects_short_frame(void)
{
    uint8_t buf[RCP_ACF_GBB_HEADER_LEN - 1] = {0};
    rcp_acf_gbb_header_t out = {0};
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME,
                       rcp_acf_decode_gbb(buf, sizeof(buf), &out, &payload, &payload_len));
}

//cfusa:test REQ-ACF-008
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

//cfusa:test REQ-ACF-041
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

//cfusa:test REQ-ACF-012
static void test_gbb_is_timed_true_only_when_valid(void)
{
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_VALID;
    TEST_ASSERT_TRUE(rcp_acf_gbb_is_timed(&hdr));
}

//cfusa:test REQ-ACF-012
static void test_gbb_is_timed_false_when_untimed(void)
{
    rcp_acf_gbb_header_t hdr = {0};

    hdr.info.mtv = RCP_ACF_MTV_UNTIMED;
    TEST_ASSERT_FALSE(rcp_acf_gbb_is_timed(&hdr));
}

//cfusa:test REQ-ACF-011
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

//cfusa:test REQ-ACF-034
static void test_classify_response_error_takes_priority(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.err = 1;
    hdr.op  = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
}

//cfusa:test REQ-ACF-035
static void test_classify_response_write(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_WRITE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_WRITE, rcp_acf_classify_response(&hdr));
}

//cfusa:test REQ-ACF-036
static void test_classify_response_read(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_READ, rcp_acf_classify_response(&hdr));
}

//cfusa:test REQ-ACF-037
static void test_classify_response_acknowledge(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.op = RCP_ACF_OP_NONE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
}

/* TC18 §11.3.1: evt[3:0] = 0xF identifies an Acknowledge regardless of op,
 * which for real decoded input is always RCP_ACF_OP_WRITE or
 * RCP_ACF_OP_READ (RCP_ACF_OP_NONE is encode-only -- see rcp_acf_op_t's
 * doc comment). Without checking evt first, a real Acknowledge response
 * with op = RCP_ACF_OP_WRITE was silently misclassified as
 * RCP_ACF_RESP_WRITE. */
//cfusa:test REQ-ACF-002
static void test_classify_response_acknowledge_from_decoded_write_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.evt = RCP_ACF_EVT_ACKNOWLEDGE;
    hdr.op  = RCP_ACF_OP_WRITE;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
}

//cfusa:test REQ-ACF-002
static void test_classify_response_acknowledge_from_decoded_read_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.evt = RCP_ACF_EVT_ACKNOWLEDGE;
    hdr.op  = RCP_ACF_OP_READ;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
}

/* TC18 §11.3.1: a rejected Acknowledge (evt[3:0] = 0xF, err = 1) is still
 * an Acknowledge, not an Error Response (§11.3.4's Error Response is the
 * distinct evt[3:0] < 0x9, err = 1 case) -- err must not take priority
 * over evt here. */
//cfusa:test REQ-ACF-002
static void test_classify_response_acknowledge_rejected_is_not_error(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    hdr.evt = RCP_ACF_EVT_ACKNOWLEDGE;
    hdr.op  = RCP_ACF_OP_WRITE;
    hdr.err = 1;
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
}

/* ── Table 30 Row 2 evt[2:0] rule ────────────────────────────────────────────── */

//cfusa:test REQ-ACF-023
static void test_evt_row2_is_plain_true_for_zero(void)
{
    TEST_ASSERT_TRUE(rcp_acf_evt_row2_is_plain(0x0));
}

//cfusa:test REQ-ACF-023
static void test_evt_row2_is_plain_false_for_reserved_values(void)
{
    uint8_t v;

    for (v = 1; v <= 6; v++) {
        TEST_ASSERT_FALSE(rcp_acf_evt_row2_is_plain(v));
    }
}

//cfusa:test REQ-ACF-023
static void test_evt_row2_is_plain_false_for_config_value(void)
{
    TEST_ASSERT_FALSE(rcp_acf_evt_row2_is_plain(0x7));
}

//cfusa:test REQ-ACF-023
static void test_evt_row2_is_plain_ignores_evt3(void)
{
    /* evt[3] (the ack-request bit) is outside evt[2:0]'s 3-bit scope --
     * a request with evt[3] set but evt[2:0] = 000b is still plain. */
    TEST_ASSERT_TRUE(rcp_acf_evt_row2_is_plain(0x8));
}

/* ── §13.5.1 compound-wait evt[2:0] comparison rule ─────────────────────────── */

//cfusa:test REQ-ACF-024
static void test_compound_wait_evt_valid_true_for_every_mode_but_reserved(void)
{
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x0));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x1));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x2));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x4));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x5));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x6));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_evt_valid(0x7));
}

//cfusa:test REQ-ACF-024
static void test_compound_wait_evt_valid_false_for_reserved(void)
{
    /* evt[2:0] = 011b is reserved regardless of evt's upper bit(s). */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_evt_valid(0x3));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_evt_valid(0xB)); /* 1011b */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_evt_valid(0xFB & 0x0Fu));
}

//cfusa:test REQ-ACF-025
static void test_compound_wait_match_false_when_status_shorter_than_payload(void)
{
    const uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t status[3]  = {0x01, 0x02, 0x03};

    /* Exact match on the shared 3-byte prefix would otherwise succeed --
     * the length rule must short-circuit before any mode-specific
     * comparison runs. */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x0, payload, sizeof(payload),
                                                   status, sizeof(status)));
}

//cfusa:test REQ-ACF-025
static void test_compound_wait_match_caps_status_to_payload_length(void)
{
    /* The specification's own SPI worked example: only the first four out
     * of a longer (here 20-byte) status report are compared when
     * byte_msg_payload has four bytes. */
    const uint8_t payload[4]  = {0x00, 0x00, 0x00, 0x02};
    uint8_t       status[20];

    memset(status, 0xAA, sizeof(status)); /* tail bytes: never read */
    status[0] = 0x00; status[1] = 0x00; status[2] = 0x00; status[3] = 0x02;
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x0, payload, sizeof(payload),
                                                  status, sizeof(status)));

    /* Changing a byte within the compared prefix must still be seen. */
    status[3] = 0x03;
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x0, payload, sizeof(payload),
                                                   status, sizeof(status)));
}

//cfusa:test REQ-ACF-026
static void test_compound_wait_match_exact_mode(void)
{
    const uint8_t payload[2] = {0x01, 0x02};
    const uint8_t equal[2]   = {0x01, 0x02};
    const uint8_t differs[2] = {0x01, 0x03};

    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x0, payload, 2, equal, 2));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x0, payload, 2, differs, 2));
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x0, NULL, 0, NULL, 0));
}

//cfusa:test REQ-ACF-027
static void test_compound_wait_match_and_ones_mask_mode(void)
{
    /* The specification's own example: byte_msg_payload = 0x00000002
     * checks whether the second IO pin (bit 1) is asserted. */
    const uint8_t payload[4]     = {0x00, 0x00, 0x00, 0x02};
    const uint8_t bit_set[4]     = {0x00, 0x00, 0x00, 0x02};
    const uint8_t bit_clear[4]   = {0x00, 0x00, 0x00, 0x00};
    const uint8_t other_bits[4]  = {0xFF, 0xFF, 0xFF, 0xFF};

    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x1, payload, 4, bit_set, 4));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x1, payload, 4, bit_clear, 4));
    /* Payload's own 0-bits are don't-care: status's other set bits (which
     * correspond to payload 0-bits) must not affect the outcome. */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x1, payload, 4, other_bits, 4));
}

//cfusa:test REQ-ACF-028
static void test_compound_wait_match_and_zeros_mask_mode(void)
{
    /* The specification's own example: byte_msg_payload = 0x00000002
     * checks whether the second IO pin (bit 1) is NOT asserted. */
    const uint8_t payload[4]        = {0x00, 0x00, 0x00, 0x02};
    const uint8_t bit_clear[4]      = {0x00, 0x00, 0x00, 0x00};
    const uint8_t bit_set[4]        = {0x00, 0x00, 0x00, 0x02};
    const uint8_t other_bits_only[4] = {0xFF, 0xFF, 0xFF, 0xFD};

    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x2, payload, 4, bit_clear, 4));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x2, payload, 4, bit_set, 4));
    /* Payload's own 0-bits are don't-care here too. */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x2, payload, 4, other_bits_only, 4));
}

//cfusa:test REQ-ACF-029
static void test_compound_wait_match_leading_quadlet_hi_word_ge(void)
{
    const uint8_t payload[4] = {0x00, 0x0A, 0x00, 0x00}; /* hi word = 10 */
    const uint8_t lower[4]   = {0x00, 0x05, 0x00, 0x00}; /* hi word = 5 */
    const uint8_t higher[4]  = {0x00, 0x0F, 0x00, 0x00}; /* hi word = 15 */
    const uint8_t equal[4]   = {0x00, 0x0A, 0x00, 0x00};

    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x4, payload, 4, lower, 4));  /* 10>=5 */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x4, payload, 4, higher, 4)); /* 10>=15 */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x4, payload, 4, equal, 4));  /* 10>=10 */
}

/* REQ-ACF-052 split off REQ-ACF-029 (c-RCP-18-tracker, issue #533): the
 * evt[2:0]=101b (<=) counterpart used to share a test function (and an
 * id) with the 100b (>=) case above -- now its own separately-testable
 * mode, matching this function's own one-mode-per-id convention
 * elsewhere (REQ-ACF-026/027/028). */
//cfusa:test REQ-ACF-052
static void test_compound_wait_match_leading_quadlet_hi_word_le(void)
{
    const uint8_t payload[4] = {0x00, 0x0A, 0x00, 0x00}; /* hi word = 10 */
    const uint8_t lower[4]   = {0x00, 0x05, 0x00, 0x00}; /* hi word = 5 */
    const uint8_t higher[4]  = {0x00, 0x0F, 0x00, 0x00}; /* hi word = 15 */
    const uint8_t equal[4]   = {0x00, 0x0A, 0x00, 0x00};

    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x5, payload, 4, lower, 4)); /* 10<=5 */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x5, payload, 4, higher, 4)); /* 10<=15 */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x5, payload, 4, equal, 4));  /* 10<=10 */
}

//cfusa:test REQ-ACF-030
static void test_compound_wait_match_leading_quadlet_lo_word_ge(void)
{
    const uint8_t payload[4] = {0xFF, 0xFF, 0x00, 0x0A}; /* lo word = 10 */
    const uint8_t lower[4]   = {0xFF, 0xFF, 0x00, 0x05}; /* lo word = 5 */
    const uint8_t higher[4]  = {0xFF, 0xFF, 0x00, 0x0F}; /* lo word = 15 */

    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x6, payload, 4, lower, 4));  /* 10>=5 */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x6, payload, 4, higher, 4)); /* 10>=15 */
}

/* REQ-ACF-053 split off REQ-ACF-030 (c-RCP-18-tracker, issue #533): the
 * evt[2:0]=111b (<=) counterpart, previously sharing a test function (and
 * an id) with the 110b (>=) case above. */
//cfusa:test REQ-ACF-053
static void test_compound_wait_match_leading_quadlet_lo_word_le(void)
{
    const uint8_t payload[4] = {0xFF, 0xFF, 0x00, 0x0A}; /* lo word = 10 */
    const uint8_t lower[4]   = {0xFF, 0xFF, 0x00, 0x05}; /* lo word = 5 */
    const uint8_t higher[4]  = {0xFF, 0xFF, 0x00, 0x0F}; /* lo word = 15 */

    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x7, payload, 4, lower, 4)); /* 10<=5 */
    TEST_ASSERT_TRUE(rcp_acf_compound_wait_match(0x7, payload, 4, higher, 4)); /* 10<=15 */
}

//cfusa:test REQ-ACF-029
//cfusa:test REQ-ACF-030
//cfusa:test REQ-ACF-052
//cfusa:test REQ-ACF-053
static void test_compound_wait_match_ge_le_rejects_short_payload(void)
{
    const uint8_t payload[3] = {0x00, 0x0A, 0x00};
    const uint8_t status[3]  = {0x00, 0x00, 0x00};

    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x4, payload, 3, status, 3));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x5, payload, 3, status, 3));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x6, payload, 3, status, 3));
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x7, payload, 3, status, 3));
}

/* REQ-ACF-051 split off REQ-ACF-025 (c-RCP-18-tracker, issue #533): the
 * reserved-evt(0x3) fallback is its own separately-testable behaviour of
 * rcp_acf_compound_wait_match(), distinct from the shared status-length
 * rule REQ-ACF-025 now names alone. */
//cfusa:test REQ-ACF-051
static void test_compound_wait_match_reserved_mode_always_false(void)
{
    const uint8_t payload[2] = {0x01, 0x02};

    /* Callers must gate on rcp_acf_compound_wait_evt_valid() first; this
     * pins the function's own defined (always-false) behavior if they
     * don't. */
    TEST_ASSERT_FALSE(rcp_acf_compound_wait_match(0x3, payload, 2, payload, 2));
}

/* ── Error response (TC18 §12.9.6 / §11.3.4) ─────────────────────────────────── */

//cfusa:test REQ-ACF-031
static void test_build_error_response_carries_bus_id_txn_and_code(void)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_bytes_t                  resp =
        rcp_acf_build_error_response((rcp_byte_bus_id_t)7, 200, RCP_ERROR_REQUEST_STORAGE_OVERFLOW);

    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.rsp);
    TEST_ASSERT_EQUAL_UINT8(7u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(200u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_REQUEST_STORAGE_OVERFLOW, payload[0]);

    rcp_bytes_free(&resp);
}

//cfusa:test REQ-ACF-031
static void test_build_error_response_never_classifies_as_acknowledge(void)
{
    /* evt = 0 (not RCP_ACF_EVT_ACKNOWLEDGE = 0xF): even though err = 1
     * alone already selects RCP_ACF_RESP_ERROR ahead of any op-based
     * fallback, this pins that an error response can never be
     * misclassified as the one evt value TC18 reserves for Acknowledge. */
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_bytes_t                  resp =
        rcp_acf_build_error_response((rcp_byte_bus_id_t)1, 1, RCP_ERROR_UNSUPPORTED_CMD);

    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_NOT_EQUAL(RCP_ACF_EVT_ACKNOWLEDGE, hdr.evt);
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&hdr));

    rcp_bytes_free(&resp);
}

/* ── Acknowledge-shaped storage-admission rejection (TC18 §11.3.1) ─────────── */

/* FIXED 2026-08-14 (issue #430, REQ-ACF-033): TC18 §11.3.1's own OTHER
 * Acknowledge shape -- "err = 1 indicates that the request has been
 * rejected. The byte_msg_payload contains an error code." -- for a
 * request that was never filed into EP request storage at all. Distinct
 * from rcp_acf_build_error_response()'s §11.3.4 Error Response shape
 * (evt[3:0] < 0x9, err = 1): same err=1/one-octet-payload wire shape,
 * but THIS function's evt = RCP_ACF_EVT_ACKNOWLEDGE (0xF) is what makes
 * rcp_acf_classify_response() recognize it as RCP_ACF_RESP_ACKNOWLEDGE
 * rather than RCP_ACF_RESP_ERROR -- the two are not interchangeable on
 * the wire, whatever their payload happens to carry. */
//cfusa:test REQ-ACF-033
static void test_build_acknowledge_rejected_response_carries_bus_id_txn_and_code(void)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_bytes_t                  resp = rcp_acf_build_acknowledge_rejected_response(
        (rcp_byte_bus_id_t)7, 200, RCP_ERROR_REQUEST_STORAGE_OVERFLOW);

    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(resp.data, resp.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_EVT_ACKNOWLEDGE, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, hdr.rsp);
    TEST_ASSERT_EQUAL_UINT8(7u, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(200u, hdr.transaction_num);
    TEST_ASSERT_EQUAL_size_t(1, payload_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ERROR_REQUEST_STORAGE_OVERFLOW, payload[0]);

    rcp_bytes_free(&resp);
}

/* Pins the distinction from rcp_acf_build_error_response() directly: same
 * transaction_num/error code, but the two builders' own responses must
 * classify differently (Acknowledge vs Error), and only the rejected-
 * acknowledge shape's own evt is 0xF. */
//cfusa:test REQ-ACF-033
static void test_build_acknowledge_rejected_response_differs_from_error_response(void)
{
    rcp_acf_byte_message_info_t ack_hdr, err_hdr;
    const uint8_t               *ack_payload, *err_payload;
    size_t                       ack_payload_len, err_payload_len;
    rcp_bytes_t                  ack_resp = rcp_acf_build_acknowledge_rejected_response(
        (rcp_byte_bus_id_t)3, 55, RCP_ERROR_UNSUPPORTED_CMD);
    rcp_bytes_t                  err_resp =
        rcp_acf_build_error_response((rcp_byte_bus_id_t)3, 55, RCP_ERROR_UNSUPPORTED_CMD);

    TEST_ASSERT_NOT_NULL(ack_resp.data);
    TEST_ASSERT_NOT_NULL(err_resp.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(ack_resp.data, ack_resp.len, &ack_hdr,
                                                      &ack_payload, &ack_payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(err_resp.data, err_resp.len, &err_hdr,
                                                      &err_payload, &err_payload_len));

    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ACKNOWLEDGE, rcp_acf_classify_response(&ack_hdr));
    TEST_ASSERT_EQUAL(RCP_ACF_RESP_ERROR, rcp_acf_classify_response(&err_hdr));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_EVT_ACKNOWLEDGE, ack_hdr.evt);
    TEST_ASSERT_NOT_EQUAL(RCP_ACF_EVT_ACKNOWLEDGE, err_hdr.evt);
    /* Both carry err=1 and the same payload octet -- only evt tells them apart. */
    TEST_ASSERT_EQUAL_UINT8(1u, ack_hdr.err);
    TEST_ASSERT_EQUAL_UINT8(1u, err_hdr.err);
    TEST_ASSERT_EQUAL_UINT8(ack_payload[0], err_payload[0]);

    rcp_bytes_free(&ack_resp);
    rcp_bytes_free(&err_resp);
}

/* ── Message-type dispatch ─────────────────────────────────────────────────── */

//cfusa:test REQ-ACF-013
static void test_peek_msg_type_reads_first_byte(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    uint8_t msg_type = 0;

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(frame.data, frame.len, &msg_type));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_ABB, msg_type);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-ACF-013
static void test_peek_msg_type_rejects_empty_buffer(void)
{
    uint8_t msg_type = 0;
    TEST_ASSERT_EQUAL(RCP_ACF_ERR_SHORT_FRAME, rcp_acf_peek_msg_type(NULL, 0, &msg_type));
}

/* ── rcp_acf_header_is_request() (REQ-ACF-050) ─────────────────────────────── */

/* REQ-ACF-050 split off REQ-ACF-021 (c-RCP-18-tracker, issue #533): a
 * standalone test of rcp_acf_header_is_request() on its own, independent
 * of test_acf_request_flags_round_trip_but_admission_now_rejects_rsp
 * (tests/test_tc18_gaps_ep.c), which bundles this function's own
 * true/false contract together with rcp_acf_request_header_constraints_
 * valid() and a full server-admission call -- reverting just this
 * function's own logic wouldn't necessarily fail that larger test in an
 * attributable way. */
//cfusa:test REQ-ACF-050
static void test_header_is_request_true_for_rsp_zero_false_for_rsp_one(void)
{
    rcp_acf_byte_message_info_t hdr = {0};

    TEST_ASSERT_TRUE(rcp_acf_header_is_request(&hdr)); /* rsp=0: a request */

    hdr.rsp = 1;
    TEST_ASSERT_FALSE(rcp_acf_header_is_request(&hdr)); /* rsp=1: a response */
}

/* ── acf_msg_length is recomputed, never trusted from the caller (REQ-ACF-006/040) ── */

/* REQ-ACF-006 (c-RCP-18-tracker, issue #533): rcp_acf_encode_abb() must
 * ignore whatever acf_msg_length the caller's header happens to carry and
 * derive it fresh from payload_len -- the golden-vector/roundtrip tests
 * above always start from a zeroed header (acf_msg_length == 0), so they
 * cannot distinguish "derived from payload_len" from "just echoed the
 * caller's own (zero) value". This test deliberately poisons
 * hdr.acf_msg_length with a wrong, nonzero value before encoding. */
//cfusa:test REQ-ACF-006
static void test_encode_abb_ignores_caller_supplied_acf_msg_length(void)
{
    uint8_t                      body[4] = {1, 2, 3, 4};
    rcp_acf_byte_message_info_t  hdr     = {0};
    rcp_bytes_t                  frame;
    rcp_acf_byte_message_info_t  out         = {0};
    const uint8_t                *payload    = NULL;
    size_t                        payload_len = 0;

    hdr.acf_msg_length = 0x1FF; /* deliberately wrong -- must be ignored */

    frame = rcp_acf_encode_abb(&hdr, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);
    /* 8 (header) + 4 (body) = 12 octets = 3 quadlets, NOT 0x1FF. */
    TEST_ASSERT_EQUAL_UINT16(0x03u, (uint16_t)(((frame.data[0] & 0x01u) << 8) | frame.data[1]));

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(3u, out.acf_msg_length);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-ACF-040
static void test_encode_gbb_ignores_caller_supplied_acf_msg_length(void)
{
    uint8_t                body[2] = {1, 2};
    rcp_acf_gbb_header_t   hdr     = {0};
    rcp_bytes_t            frame;
    rcp_acf_gbb_header_t   out         = {0};
    const uint8_t          *payload    = NULL;
    size_t                  payload_len = 0;

    hdr.info.acf_msg_length = 0x1FF; /* deliberately wrong -- must be ignored */

    frame = rcp_acf_encode_gbb(&hdr, body, sizeof(body));
    TEST_ASSERT_NOT_NULL(frame.data);
    /* 16 (header+ts) + 2 (body) = 18 -> pad to 20 octets = 5 quadlets. */
    TEST_ASSERT_EQUAL_UINT16(0x05u, (uint16_t)(((frame.data[0] & 0x01u) << 8) | frame.data[1]));

    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_gbb(frame.data, frame.len, &out, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT16(5u, out.info.acf_msg_length);

    rcp_bytes_free(&frame);
}

/* ── strerror ──────────────────────────────────────────────────────────────── */

//cfusa:test REQ-ACF-001
static void test_acf_strerror_unique_nonempty(void)
{
    const rcp_acf_errc_t codes[] = {
        RCP_ACF_OK, RCP_ACF_ERR_SHORT_FRAME, RCP_ACF_ERR_BAD_MSG_TYPE,
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

    RUN_TEST(test_msg_type_abb_constant);
    RUN_TEST(test_msg_type_gbb_constant);
    RUN_TEST(test_gbb_header_is_exactly_8_bytes_longer_than_abb);

    RUN_TEST(test_golden_figure19_abb_prelcrc_quadlets_and_pad);
    RUN_TEST(test_golden_figure20_gbb_prelcrc_quadlets_and_pad);

    RUN_TEST(test_pack_header_bit_positions);
    RUN_TEST(test_unpack_header_is_pack_header_inverse);
    RUN_TEST(test_unpack_header_bit_positions_from_raw_bytes);
    RUN_TEST(test_unpack_header_reads_full_11_bit_bus_id);
    RUN_TEST(test_peek_gbb_request_type);
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
    RUN_TEST(test_gbb_roundtrip_remaining_header_fields);
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
    RUN_TEST(test_classify_response_acknowledge_from_decoded_write_op);
    RUN_TEST(test_classify_response_acknowledge_from_decoded_read_op);
    RUN_TEST(test_classify_response_acknowledge_rejected_is_not_error);
    RUN_TEST(test_evt_row2_is_plain_true_for_zero);
    RUN_TEST(test_evt_row2_is_plain_false_for_reserved_values);
    RUN_TEST(test_evt_row2_is_plain_false_for_config_value);
    RUN_TEST(test_evt_row2_is_plain_ignores_evt3);

    RUN_TEST(test_compound_wait_evt_valid_true_for_every_mode_but_reserved);
    RUN_TEST(test_compound_wait_evt_valid_false_for_reserved);
    RUN_TEST(test_compound_wait_match_false_when_status_shorter_than_payload);
    RUN_TEST(test_compound_wait_match_caps_status_to_payload_length);
    RUN_TEST(test_compound_wait_match_exact_mode);
    RUN_TEST(test_compound_wait_match_and_ones_mask_mode);
    RUN_TEST(test_compound_wait_match_and_zeros_mask_mode);
    RUN_TEST(test_compound_wait_match_leading_quadlet_hi_word_ge);
    RUN_TEST(test_compound_wait_match_leading_quadlet_hi_word_le);
    RUN_TEST(test_compound_wait_match_leading_quadlet_lo_word_ge);
    RUN_TEST(test_compound_wait_match_leading_quadlet_lo_word_le);
    RUN_TEST(test_compound_wait_match_ge_le_rejects_short_payload);
    RUN_TEST(test_compound_wait_match_reserved_mode_always_false);

    RUN_TEST(test_build_error_response_carries_bus_id_txn_and_code);
    RUN_TEST(test_build_error_response_never_classifies_as_acknowledge);
    RUN_TEST(test_build_acknowledge_rejected_response_carries_bus_id_txn_and_code);
    RUN_TEST(test_build_acknowledge_rejected_response_differs_from_error_response);

    RUN_TEST(test_peek_msg_type_reads_first_byte);
    RUN_TEST(test_peek_msg_type_rejects_empty_buffer);

    RUN_TEST(test_header_is_request_true_for_rsp_zero_false_for_rsp_one);
    RUN_TEST(test_encode_abb_ignores_caller_supplied_acf_msg_length);
    RUN_TEST(test_encode_gbb_ignores_caller_supplied_acf_msg_length);

    RUN_TEST(test_acf_strerror_unique_nonempty);

    return UNITY_END();
}
