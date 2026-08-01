/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-DISC-001
//cfusa:test REQ-DISC-002
//cfusa:test REQ-DISC-003
//cfusa:test REQ-DISC-004
//cfusa:test REQ-DISC-005
//cfusa:test REQ-DISC-006
//cfusa:test REQ-DISC-007
//cfusa:test REQ-DISC-008
//cfusa:test REQ-DISC-009
//cfusa:test REQ-DISC-010
//cfusa:test REQ-DISC-011
//cfusa:test REQ-DISC-012
//cfusa:test REQ-DISC-013
//cfusa:test REQ-DISC-014
//cfusa:test REQ-DISC-015
//cfusa:test REQ-DISC-016
//cfusa:test REQ-DISC-017
//cfusa:test REQ-DISC-018
//cfusa:test REQ-DISC-019
//cfusa:test REQ-DISC-020
//cfusa:test REQ-DISC-021
//cfusa:test REQ-DISC-022
//cfusa:test REQ-DISC-023
//cfusa:test REQ-DISC-024
//cfusa:test REQ-DISC-025
//cfusa:test REQ-DISC-026
//cfusa:test REQ-DISC-027
//cfusa:test REQ-DISC-028
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/discovery.h>
#include <rcp/fragment.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t CLIENT_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t SERVER_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
static const uint8_t OTHER_MAC[6]  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x03};

/* ── NTSCF-only rule ────────────────────────────────────────────────────────── */

static void test_should_drop_true_for_tscf(void)
{
    TEST_ASSERT_TRUE(rcp_discovery_should_drop(RCP_AVTP_SUBTYPE_TSCF));
}

static void test_should_drop_false_for_ntscf(void)
{
    TEST_ASSERT_FALSE(rcp_discovery_should_drop(RCP_AVTP_SUBTYPE_NTSCF));
}

static void test_should_drop_true_for_unrecognized_subtype(void)
{
    TEST_ASSERT_TRUE(rcp_discovery_should_drop(0x00));
}

/* ── Discovery request round-trip ──────────────────────────────────────────── */

static void test_request_round_trip(void)
{
    rcp_stream_id_t client = rcp_stream_id_make(CLIENT_MAC, 7);
    rcp_bytes_t frame = rcp_discovery_encode_request(client, 12, 42);
    rcp_discovery_request_t req;

    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_OK, rcp_discovery_decode_request(frame.data, frame.len, &req));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(client, req.requester));
    TEST_ASSERT_EQUAL_UINT8(12, req.read_size);
    TEST_ASSERT_EQUAL_UINT8(42, req.transaction_num);

    rcp_bytes_free(&frame);
}

static void test_request_addressed_to_discovery_bus(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t acf_frame;
    rcp_avtp_ntscf_header_t ntscf_hdr = {0};
    rcp_bytes_t frame;
    rcp_avtp_ntscf_header_t decoded_ntscf;
    const uint8_t *payload;
    size_t payload_len;

    hdr.byte_bus_id = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    hdr.op = RCP_ACF_OP_READ;
    hdr.read_size_or_segment_num = 12;
    acf_frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    ntscf_hdr.sv = 1;
    ntscf_hdr.stream_id = rcp_stream_id_make(CLIENT_MAC, 1);
    frame = rcp_avtp_encode_ntscf(&ntscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame.data, frame.len, &decoded_ntscf, &payload, &payload_len));
    TEST_ASSERT_EQUAL_HEX8(RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID, payload[3]);

    rcp_bytes_free(&frame);
}

static void test_request_dropped_when_tscf_headed(void)
{
    rcp_avtp_tscf_header_t tscf_hdr = {0};
    rcp_acf_byte_message_info_t acf_hdr = {0};
    rcp_bytes_t acf_frame;
    rcp_bytes_t frame;
    rcp_discovery_request_t req;

    acf_hdr.byte_bus_id = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    acf_hdr.op = RCP_ACF_OP_READ;
    acf_frame = rcp_acf_encode_abb(&acf_hdr, NULL, 0);

    tscf_hdr.sv = 1;
    tscf_hdr.stream_id = rcp_stream_id_make(CLIENT_MAC, 1);
    frame = rcp_avtp_encode_tscf(&tscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_NOT_NTSCF,
                       rcp_discovery_decode_request(frame.data, frame.len, &req));

    rcp_bytes_free(&frame);
}

static void test_request_rejects_non_abb_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t acf_frame;
    rcp_avtp_ntscf_header_t ntscf_hdr = {0};
    rcp_bytes_t frame;
    rcp_discovery_request_t req;

    gbb_hdr.info.byte_bus_id = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    gbb_hdr.info.op = RCP_ACF_OP_READ;
    acf_frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    ntscf_hdr.sv = 1;
    ntscf_hdr.stream_id = rcp_stream_id_make(CLIENT_MAC, 1);
    frame = rcp_avtp_encode_ntscf(&ntscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_BAD_MSG_TYPE,
                       rcp_discovery_decode_request(frame.data, frame.len, &req));

    rcp_bytes_free(&frame);
}

static void test_request_rejects_wrong_byte_bus_id(void)
{
    rcp_stream_id_t client = rcp_stream_id_make(CLIENT_MAC, 1);
    rcp_bytes_t frame;
    rcp_discovery_request_t req;
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t acf_frame;
    rcp_avtp_ntscf_header_t ntscf_hdr = {0};

    hdr.byte_bus_id = (rcp_byte_bus_id_t)7u; /* not the discovery bus */
    hdr.op = RCP_ACF_OP_READ;
    acf_frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    ntscf_hdr.sv = 1;
    ntscf_hdr.stream_id = client;
    frame = rcp_avtp_encode_ntscf(&ntscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_WRONG_BUS,
                       rcp_discovery_decode_request(frame.data, frame.len, &req));

    rcp_bytes_free(&frame);
}

static void test_request_rejects_wrong_op(void)
{
    rcp_stream_id_t client = rcp_stream_id_make(CLIENT_MAC, 1);
    rcp_bytes_t frame;
    rcp_discovery_request_t req;
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t acf_frame;
    rcp_avtp_ntscf_header_t ntscf_hdr = {0};

    hdr.byte_bus_id = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    hdr.op = RCP_ACF_OP_WRITE; /* discovery is always a read */
    acf_frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    ntscf_hdr.sv = 1;
    ntscf_hdr.stream_id = client;
    frame = rcp_avtp_encode_ntscf(&ntscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_WRONG_OP,
                       rcp_discovery_decode_request(frame.data, frame.len, &req));

    rcp_bytes_free(&frame);
}

static void test_request_decode_short_frame(void)
{
    rcp_discovery_request_t req;
    uint8_t tiny[2] = {RCP_AVTP_SUBTYPE_NTSCF, 0};

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_SHORT_FRAME,
                       rcp_discovery_decode_request(tiny, sizeof(tiny), &req));
}

static void test_request_decode_empty_buffer(void)
{
    rcp_discovery_request_t req;

    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_SHORT_FRAME, rcp_discovery_decode_request(NULL, 0, &req));
}

/* ── Discovery response ─────────────────────────────────────────────────────── */

static rcp_regmap_general_t sample_map(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);
    map.magic           = 0xC0FFEE01u;
    /* A 32-bit value whose upper half is non-zero, so a 16-bit
     * svr_version field cannot round-trip it -- see
     * test_response_general_slice_octet_layout(). */
    map.svr_version     = 0x00010501u;
    map.vendor_id       = 0x1234u;
    map.device_id       = 0x5678u;
    map.svr_ep_count    = 9u;
    return map;
}

static void test_response_round_trip_exact_slice_len(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    rcp_bytes_t frame = rcp_discovery_encode_response(&map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, 9, server);
    rcp_discovery_result_t result;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_DISCOVERY_OK, rcp_discovery_decode_response(frame.data, frame.len, &result));

    TEST_ASSERT_TRUE(result.valid);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(server, result.server_stream_id));
    TEST_ASSERT_EQUAL_UINT32(map.magic, result.magic);
    TEST_ASSERT_EQUAL_UINT32(map.svr_version, result.svr_version);
    TEST_ASSERT_EQUAL_UINT16(map.vendor_id, result.vendor_id);
    TEST_ASSERT_EQUAL_UINT16(map.device_id, result.device_id);
    TEST_ASSERT_EQUAL_UINT16(map.svr_ep_count, result.svr_ep_count);

    rcp_bytes_free(&frame);
}

/* TC18 v0.5.1_RC §12.7.5 "RC Server Register map - General part",
 * Table 18 "RC Server configuration static part". The absolute addresses
 * and widths of the leading, device-recognition part of the block are:
 *
 *   0x0000  svr_oa_tc18_magic_nr   32 bit  R
 *   0x0004  svr_version            32 bit  R   OATC18 RC Protocol Version
 *                                              Supported
 *   0x0008  svr_vendor_id          16 bit  R
 *   0x000A  svr_device_id          16 bit  R
 *   0x000C  svr_ep_count           16 bit  R
 *   0x000E  svr_req_stream_max      8 bit  R
 *
 * svr_version is 32 bit, so vendor_id starts at 0x0008 -- not 0x0006, as
 * a 16-bit svr_version would put it. This test pins each field to its
 * cited absolute address in the encoded payload, so the two-octet
 * regression (which shifted vendor_id, device_id and svr_ep_count each
 * two octets early, misparsing all three for any conforming peer) cannot
 * come back unnoticed. */
static void test_response_general_slice_octet_layout(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    rcp_bytes_t frame = rcp_discovery_encode_response(&map,
                                                       (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN,
                                                       9, server);
    rcp_avtp_ntscf_header_t     ntscf_hdr;
    const uint8_t              *ntscf_payload;
    size_t                      ntscf_payload_len;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *p;
    size_t                      payload_len;

    rcp_avtp_decode_ntscf(frame.data, frame.len, &ntscf_hdr, &ntscf_payload, &ntscf_payload_len);
    rcp_acf_decode_abb(ntscf_payload, ntscf_payload_len, &hdr, &p, &payload_len);

    /* magic (0x0000..0x0003) + svr_version (0x0004..0x0007) +
     * vendor_id (0x0008..0x0009) + device_id (0x000A..0x000B) +
     * svr_ep_count (0x000C..0x000D) = 14 octets. */
    TEST_ASSERT_EQUAL_UINT((size_t)14u, RCP_DISCOVERY_GENERAL_SLICE_LEN);
    TEST_ASSERT_EQUAL_UINT(RCP_DISCOVERY_GENERAL_SLICE_LEN, payload_len);

    /* 0x0000 svr_oa_tc18_magic_nr, 32 bit big-endian: 0xC0FFEE01 */
    TEST_ASSERT_EQUAL_UINT8(0xC0u, p[0x00]);
    TEST_ASSERT_EQUAL_UINT8(0xFFu, p[0x01]);
    TEST_ASSERT_EQUAL_UINT8(0xEEu, p[0x02]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, p[0x03]);
    /* 0x0004 svr_version, 32 bit big-endian: 0x00010501 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, p[0x04]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, p[0x05]);
    TEST_ASSERT_EQUAL_UINT8(0x05u, p[0x06]);
    TEST_ASSERT_EQUAL_UINT8(0x01u, p[0x07]);
    /* 0x0008 svr_vendor_id, 16 bit big-endian: 0x1234 */
    TEST_ASSERT_EQUAL_UINT8(0x12u, p[0x08]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, p[0x09]);
    /* 0x000A svr_device_id, 16 bit big-endian: 0x5678 */
    TEST_ASSERT_EQUAL_UINT8(0x56u, p[0x0A]);
    TEST_ASSERT_EQUAL_UINT8(0x78u, p[0x0B]);
    /* 0x000C svr_ep_count, 16 bit big-endian: 9 */
    TEST_ASSERT_EQUAL_UINT8(0x00u, p[0x0C]);
    TEST_ASSERT_EQUAL_UINT8(0x09u, p[0x0D]);

    rcp_bytes_free(&frame);
}

static void test_response_payload_len_always_equals_read_size(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    rcp_bytes_t frame_small = rcp_discovery_encode_response(&map, 4, 1, server);
    rcp_bytes_t frame_large = rcp_discovery_encode_response(&map, 40, 1, server);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t *payload;
    size_t payload_len;
    rcp_avtp_ntscf_header_t ntscf_hdr;
    const uint8_t *ntscf_payload;
    size_t ntscf_payload_len;

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame_small.data, frame_small.len, &ntscf_hdr,
                                             &ntscf_payload, &ntscf_payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(ntscf_payload, ntscf_payload_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT(4, payload_len);

    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(frame_large.data, frame_large.len, &ntscf_hdr,
                                             &ntscf_payload, &ntscf_payload_len));
    TEST_ASSERT_EQUAL(RCP_ACF_OK,
                       rcp_acf_decode_abb(ntscf_payload, ntscf_payload_len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT(40, payload_len);

    rcp_bytes_free(&frame_small);
    rcp_bytes_free(&frame_large);
}

static void test_response_truncated_slice_when_read_size_small(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    /* read_size of 4 -- only room for the magic field */
    rcp_bytes_t frame = rcp_discovery_encode_response(&map, 4, 1, server);
    rcp_discovery_result_t result;

    /* Too short to extract a full generic slice -- decode_response must
     * treat this as short, not silently fabricate zeros for the missing
     * fields. */
    TEST_ASSERT_EQUAL(RCP_DISCOVERY_ERR_SHORT_FRAME,
                       rcp_discovery_decode_response(frame.data, frame.len, &result));

    rcp_bytes_free(&frame);
}

static void test_response_zero_fills_beyond_general_slice(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    rcp_bytes_t frame = rcp_discovery_encode_response(&map, (uint8_t)(RCP_DISCOVERY_GENERAL_SLICE_LEN + 4), 1, server);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t *payload;
    size_t payload_len;
    rcp_avtp_ntscf_header_t ntscf_hdr;
    const uint8_t *ntscf_payload;
    size_t ntscf_payload_len;
    size_t i;

    rcp_avtp_decode_ntscf(frame.data, frame.len, &ntscf_hdr, &ntscf_payload, &ntscf_payload_len);
    rcp_acf_decode_abb(ntscf_payload, ntscf_payload_len, &hdr, &payload, &payload_len);

    TEST_ASSERT_EQUAL_UINT(RCP_DISCOVERY_GENERAL_SLICE_LEN + 4, payload_len);
    for (i = RCP_DISCOVERY_GENERAL_SLICE_LEN; i < payload_len; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, payload[i]);
    }

    rcp_bytes_free(&frame);
}

/* ── Fragmented response (Phase 20, fragment.h) ────────────────────────────── */

static void test_fragment_count_one_when_unfragmented(void)
{
    TEST_ASSERT_EQUAL_UINT(1, rcp_discovery_response_fragment_count(12, 100));
    TEST_ASSERT_EQUAL_UINT(1, rcp_discovery_response_fragment_count(0, 0));
}

static void test_fragment_unfragmented_matches_single_frame_path(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    rcp_bytes_t plain = rcp_discovery_encode_response(
        &map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, 9, server);
    rcp_bytes_t fragmented[1];
    size_t count;

    TEST_ASSERT_NOT_NULL(plain.data);

    count = rcp_discovery_encode_response_fragmented(
        &map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, 9, server, 255, fragmented);
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_UINT(plain.len, fragmented[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain.data, fragmented[0].data, plain.len);

    rcp_bytes_free(&plain);
    rcp_bytes_free(&fragmented[0]);
}

/* Closes the deferred single-AVTPDU-worst-case test noted at milestone 63
 * (v0.63.0): exercises fragment.h's ms/segment_num mechanism against this
 * module's own NTSCF+ACF wire codec end-to-end, using a deliberately
 * small max_fragment_payload -- see this endpoint's own header comment
 * for why read_size's one-octet width means genuine discovery traffic
 * never actually needs more than one fragment in practice; this test
 * proves the mechanism composes correctly regardless. */
static void test_fragment_deliberately_small_cap_round_trip(void)
{
    rcp_regmap_general_t       map    = sample_map();
    rcp_stream_id_t             server = rcp_stream_id_make(SERVER_MAC, 3);
    uint8_t                     read_size = 20;
    size_t                      max_fragment_payload = 6;
    size_t                      count;
    rcp_bytes_t                 frames[4];
    rcp_fragment_reassembler_t  reasm;
    size_t                      i;

    count = rcp_discovery_response_fragment_count(read_size, max_fragment_payload);
    TEST_ASSERT_EQUAL_UINT(4, count); /* ceil(20/6) */
    TEST_ASSERT_TRUE(count <= (sizeof(frames) / sizeof(frames[0])));

    count = rcp_discovery_encode_response_fragmented(&map, read_size, 42, server,
                                                       max_fragment_payload, frames);
    TEST_ASSERT_EQUAL_UINT(4, count);

    rcp_fragment_reassembler_init(&reasm, read_size);
    for (i = 0; i < count; i++) {
        rcp_stream_id_t              from_stream;
        bool                          ms;
        uint8_t                      segnum;
        const uint8_t                *payload;
        size_t                        payload_len;
        rcp_fragment_reasm_result_t   rc;

        TEST_ASSERT_EQUAL(RCP_DISCOVERY_OK,
            rcp_discovery_decode_response_fragment(frames[i].data, frames[i].len, &from_stream,
                                                     &ms, &segnum, &payload, &payload_len));
        TEST_ASSERT_TRUE(rcp_stream_id_equal(server, from_stream));

        rc = rcp_fragment_reassembler_feed(&reasm, ms, segnum, payload, payload_len);
        if (i + 1 < count) {
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);
        } else {
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);
        }
    }

    {
        const uint8_t          *reassembled;
        size_t                   reassembled_len;
        rcp_discovery_result_t   result;

        rcp_fragment_reassembler_get(&reasm, &reassembled, &reassembled_len);
        TEST_ASSERT_EQUAL_UINT(read_size, reassembled_len);

        TEST_ASSERT_EQUAL(RCP_DISCOVERY_OK,
            rcp_discovery_decode_reassembled_response(reassembled, reassembled_len, server,
                                                        &result));
        TEST_ASSERT_TRUE(result.valid);
        TEST_ASSERT_TRUE(rcp_stream_id_equal(server, result.server_stream_id));
        TEST_ASSERT_EQUAL_UINT32(map.magic, result.magic);
        TEST_ASSERT_EQUAL_UINT32(map.svr_version, result.svr_version);
        TEST_ASSERT_EQUAL_UINT16(map.vendor_id, result.vendor_id);
        TEST_ASSERT_EQUAL_UINT16(map.device_id, result.device_id);
        TEST_ASSERT_EQUAL_UINT16(map.svr_ep_count, result.svr_ep_count);
    }

    rcp_fragment_reassembler_destroy(&reasm);
    for (i = 0; i < count; i++) rcp_bytes_free(&frames[i]);
}

static void test_fragment_encode_disabled_when_zero_cap_and_oversized(void)
{
    rcp_regmap_general_t map = sample_map();
    rcp_stream_id_t server = rcp_stream_id_make(SERVER_MAC, 3);
    rcp_bytes_t frames[4];
    size_t count = rcp_discovery_encode_response_fragmented(&map, 20, 1, server, 0, frames);

    TEST_ASSERT_EQUAL_UINT(0, count);
}

/* ── Discovery-stream claiming ──────────────────────────────────────────────── */

static void test_claim_init_is_open_and_unheld(void)
{
    rcp_discovery_claim_t claim;

    rcp_discovery_claim_init(&claim, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);

    TEST_ASSERT_FALSE(claim.held);
    TEST_ASSERT_EQUAL_UINT32(RCP_DISCOVERY_DEFAULT_TIMEOUT_MS, claim.timeout_ms);
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_open(&claim, 0));
}

static void test_claim_note_request_grants_when_open(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);

    TEST_ASSERT_FALSE(rcp_discovery_claim_is_open(&claim, 1000));
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1000));
}

static void test_claim_note_request_does_not_preempt_active_claimant(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);
    rcp_stream_id_t b = rcp_stream_id_make(OTHER_MAC, 2);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);
    rcp_discovery_claim_note_request(&claim, b, 1005); /* still within window */

    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1005));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, b, 1005));
}

static void test_claim_lapses_after_timeout(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);

    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1019));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, a, 1020));
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_open(&claim, 1020));
}

static void test_claim_reopens_to_new_claimant_after_lapse(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);
    rcp_stream_id_t b = rcp_stream_id_make(OTHER_MAC, 2);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);
    rcp_discovery_claim_note_request(&claim, b, 1030); /* after lapse */

    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, b, 1030));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, a, 1030));
}

static void test_claim_config_write_refreshes_deadline_for_claimant(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);

    TEST_ASSERT_TRUE(rcp_discovery_claim_note_config_write(&claim, a, 1015));
    /* Without the refresh the claim would have lapsed at 1020; the write
     * at t=1015 should have pushed the deadline out to 1035. */
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1025));
}

static void test_claim_config_write_rejected_for_non_claimant(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);
    rcp_stream_id_t b = rcp_stream_id_make(OTHER_MAC, 2);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);

    TEST_ASSERT_FALSE(rcp_discovery_claim_note_config_write(&claim, b, 1005));
    /* a's deadline must be unaffected by b's rejected attempt. */
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1019));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, a, 1020));
}

static void test_claim_config_write_never_resurrects_lapsed_claim(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);

    TEST_ASSERT_FALSE(rcp_discovery_claim_note_config_write(&claim, a, 1025)); /* already lapsed */
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_open(&claim, 1025));
}

static void test_claim_is_claimant_false_when_never_held(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);

    rcp_discovery_claim_init(&claim, 20);

    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, a, 0));
}

static void test_claim_release_is_unconditional(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t a = rcp_stream_id_make(CLIENT_MAC, 1);

    rcp_discovery_claim_init(&claim, 20);
    rcp_discovery_claim_note_request(&claim, a, 1000);
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_open(&claim, 1000));

    rcp_discovery_claim_release(&claim);

    TEST_ASSERT_TRUE(rcp_discovery_claim_is_open(&claim, 1000));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, a, 1000));
}

/* ── Client-side discovery result persistence ──────────────────────────────── */

static rcp_discovery_result_t sample_result(const uint8_t mac[6], uint16_t unique_id, uint16_t device_id)
{
    rcp_discovery_result_t r = {0};

    r.valid            = true;
    r.server_stream_id = rcp_stream_id_make(mac, unique_id);
    r.magic            = 0xAAAAAAAAu;
    r.svr_version      = 1;
    r.vendor_id        = 2;
    r.device_id        = device_id;
    r.svr_ep_count     = 3;
    return r;
}

static void test_cache_starts_empty(void)
{
    rcp_discovery_cache_t cache;

    rcp_discovery_cache_init(&cache);
    TEST_ASSERT_EQUAL_UINT(0, rcp_discovery_cache_len(&cache));

    rcp_discovery_cache_destroy(&cache);
}

static void test_cache_put_then_find(void)
{
    rcp_discovery_cache_t cache;
    rcp_discovery_result_t r = sample_result(SERVER_MAC, 1, 100);
    const rcp_discovery_result_t *found;

    rcp_discovery_cache_init(&cache);
    TEST_ASSERT_TRUE(rcp_discovery_cache_put(&cache, &r));
    TEST_ASSERT_EQUAL_UINT(1, rcp_discovery_cache_len(&cache));

    found = rcp_discovery_cache_find(&cache, r.server_stream_id);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_UINT16(100, found->device_id);

    rcp_discovery_cache_destroy(&cache);
}

static void test_cache_put_updates_existing_entry_in_place(void)
{
    rcp_discovery_cache_t cache;
    rcp_discovery_result_t r1 = sample_result(SERVER_MAC, 1, 100);
    rcp_discovery_result_t r2 = sample_result(SERVER_MAC, 1, 200); /* same stream_id */
    const rcp_discovery_result_t *found;

    rcp_discovery_cache_init(&cache);
    rcp_discovery_cache_put(&cache, &r1);
    rcp_discovery_cache_put(&cache, &r2);

    TEST_ASSERT_EQUAL_UINT(1, rcp_discovery_cache_len(&cache)); /* updated, not appended */

    found = rcp_discovery_cache_find(&cache, r1.server_stream_id);
    TEST_ASSERT_EQUAL_UINT16(200, found->device_id);

    rcp_discovery_cache_destroy(&cache);
}

static void test_cache_find_miss_returns_null(void)
{
    rcp_discovery_cache_t cache;
    rcp_stream_id_t missing = rcp_stream_id_make(OTHER_MAC, 99);

    rcp_discovery_cache_init(&cache);
    TEST_ASSERT_NULL(rcp_discovery_cache_find(&cache, missing));

    rcp_discovery_cache_destroy(&cache);
}

static void test_cache_grows_past_initial_capacity(void)
{
    rcp_discovery_cache_t cache;
    size_t i;

    rcp_discovery_cache_init(&cache);
    for (i = 0; i < 40; i++) {
        rcp_discovery_result_t r = sample_result(SERVER_MAC, (uint16_t)i, (uint16_t)i);
        TEST_ASSERT_TRUE(rcp_discovery_cache_put(&cache, &r));
    }
    TEST_ASSERT_EQUAL_UINT(40, rcp_discovery_cache_len(&cache));

    rcp_discovery_cache_destroy(&cache);
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_unique_nonempty(void)
{
    rcp_discovery_errc_t codes[] = {
        RCP_DISCOVERY_OK,
        RCP_DISCOVERY_ERR_SHORT_FRAME,
        RCP_DISCOVERY_ERR_NOT_NTSCF,
        RCP_DISCOVERY_ERR_BAD_MSG_TYPE,
        RCP_DISCOVERY_ERR_WRONG_BUS,
        RCP_DISCOVERY_ERR_WRONG_OP,
    };
    size_t n = sizeof(codes) / sizeof(codes[0]);
    size_t i, j;

    for (i = 0; i < n; i++) {
        const char *s = rcp_discovery_strerror(codes[i]);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(strcmp(s, rcp_discovery_strerror(codes[j])) != 0);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_should_drop_true_for_tscf);
    RUN_TEST(test_should_drop_false_for_ntscf);
    RUN_TEST(test_should_drop_true_for_unrecognized_subtype);

    RUN_TEST(test_request_round_trip);
    RUN_TEST(test_request_addressed_to_discovery_bus);
    RUN_TEST(test_request_dropped_when_tscf_headed);
    RUN_TEST(test_request_rejects_non_abb_msg_type);
    RUN_TEST(test_request_rejects_wrong_byte_bus_id);
    RUN_TEST(test_request_rejects_wrong_op);
    RUN_TEST(test_request_decode_short_frame);
    RUN_TEST(test_request_decode_empty_buffer);

    RUN_TEST(test_response_round_trip_exact_slice_len);
    RUN_TEST(test_response_general_slice_octet_layout);
    RUN_TEST(test_response_payload_len_always_equals_read_size);
    RUN_TEST(test_response_truncated_slice_when_read_size_small);
    RUN_TEST(test_response_zero_fills_beyond_general_slice);

    RUN_TEST(test_fragment_count_one_when_unfragmented);
    RUN_TEST(test_fragment_unfragmented_matches_single_frame_path);
    RUN_TEST(test_fragment_deliberately_small_cap_round_trip);
    RUN_TEST(test_fragment_encode_disabled_when_zero_cap_and_oversized);

    RUN_TEST(test_claim_init_is_open_and_unheld);
    RUN_TEST(test_claim_note_request_grants_when_open);
    RUN_TEST(test_claim_note_request_does_not_preempt_active_claimant);
    RUN_TEST(test_claim_lapses_after_timeout);
    RUN_TEST(test_claim_reopens_to_new_claimant_after_lapse);
    RUN_TEST(test_claim_config_write_refreshes_deadline_for_claimant);
    RUN_TEST(test_claim_config_write_rejected_for_non_claimant);
    RUN_TEST(test_claim_config_write_never_resurrects_lapsed_claim);
    RUN_TEST(test_claim_is_claimant_false_when_never_held);
    RUN_TEST(test_claim_release_is_unconditional);

    RUN_TEST(test_cache_starts_empty);
    RUN_TEST(test_cache_put_then_find);
    RUN_TEST(test_cache_put_updates_existing_entry_in_place);
    RUN_TEST(test_cache_find_miss_returns_null);
    RUN_TEST(test_cache_grows_past_initial_capacity);

    RUN_TEST(test_strerror_unique_nonempty);

    return UNITY_END();
}
