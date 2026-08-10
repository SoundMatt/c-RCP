/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-MDIO-001
//cfusa:test REQ-MDIO-002
//cfusa:test REQ-MDIO-003
//cfusa:test REQ-MDIO-004
//cfusa:test REQ-MDIO-005
//cfusa:test REQ-MDIO-006
//cfusa:test REQ-MDIO-007
//cfusa:test REQ-MDIO-008
//cfusa:test REQ-MDIO-009
//cfusa:test REQ-MDIO-010
//cfusa:test REQ-MDIO-011
//cfusa:test REQ-MDIO-012
//cfusa:test REQ-MDIO-013
//cfusa:test REQ-MDIO-014
//cfusa:test REQ-MDIO-015
//cfusa:test REQ-MDIO-016
//cfusa:test REQ-MDIO-017
//cfusa:test REQ-MDIO-018
//cfusa:test REQ-MDIO-019
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_mdio.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_ep_mdio_addr_t clause22_addr(uint8_t prtad, uint16_t regad)
{
    rcp_ep_mdio_addr_t addr;

    addr.clause = RCP_EP_MDIO_CLAUSE_22;
    addr.prtad  = prtad;
    addr.devad  = 0;
    addr.regad  = regad;
    return addr;
}

static rcp_ep_mdio_addr_t clause45_addr(uint8_t prtad, uint8_t devad, uint16_t regad)
{
    rcp_ep_mdio_addr_t addr;

    addr.clause = RCP_EP_MDIO_CLAUSE_45;
    addr.prtad  = prtad;
    addr.devad  = devad;
    addr.regad  = regad;
    return addr;
}

/* ── addr_valid ─────────────────────────────────────────────────────────────── */

static void test_addr_valid_clause22_in_range(void)
{
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause22_addr(0, 0)));
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause22_addr(0x1F, 0x1F)));
}

static void test_addr_valid_clause22_rejects_nonzero_devad(void)
{
    rcp_ep_mdio_addr_t addr = clause22_addr(1, 1);

    addr.devad = 1;
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(addr));
}

static void test_addr_valid_clause22_rejects_regad_above_5_bits(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause22_addr(0, 0x20)));
}

static void test_addr_valid_clause45_in_range(void)
{
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause45_addr(0, 0, 0)));
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause45_addr(0x1F, 0x1F, 0xFFFF)));
}

static void test_addr_valid_clause45_rejects_devad_above_5_bits(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause45_addr(0, 0x20, 0)));
}

static void test_addr_valid_rejects_prtad_above_5_bits_either_clause(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause22_addr(0x20, 0)));
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause45_addr(0x20, 0, 0)));
}

static void test_addr_valid_rejects_unknown_clause(void)
{
    rcp_ep_mdio_addr_t addr = clause22_addr(0, 0);

    addr.clause = (rcp_ep_mdio_clause_t)2;
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(addr));
}

/* ── burst_next_regad ──────────────────────────────────────────────────────── */

static void test_burst_next_regad_clause22_increments(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_22, 0));
}

static void test_burst_next_regad_clause22_wraps_at_5_bits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_22, 0x1F));
}

static void test_burst_next_regad_clause45_increments(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x1235, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_45, 0x1234));
}

static void test_burst_next_regad_clause45_wraps_at_16_bits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_45, 0xFFFF));
}

static void test_burst_next_regad_unknown_clause_unchanged(void)
{
    TEST_ASSERT_EQUAL_UINT16(42, rcp_ep_mdio_burst_next_regad((rcp_ep_mdio_clause_t)2, 42));
}

/* ── Register-word packing ─────────────────────────────────────────────────── */

static void test_word_encode_decode_round_trip(void)
{
    uint8_t buf[2];

    rcp_ep_mdio_word_encode(0xBEEF, buf);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, rcp_ep_mdio_word_decode(buf));
}

static void test_word_encode_is_big_endian(void)
{
    uint8_t buf[2];

    rcp_ep_mdio_word_encode(0x1234, buf);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[1]);
}

static void test_pack_len(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, rcp_ep_mdio_pack_len(0));
    TEST_ASSERT_EQUAL_UINT32(6, rcp_ep_mdio_pack_len(3));
}

static void test_pack_words_round_trip(void)
{
    uint16_t    words[3] = {0x0001, 0xBEEF, 0xFFFF};
    rcp_bytes_t packed   = rcp_ep_mdio_pack_words(words, 3);
    size_t      word_count = 0;
    size_t      i;

    TEST_ASSERT_NOT_NULL(packed.data);
    TEST_ASSERT_EQUAL_UINT32(6, packed.len);
    TEST_ASSERT_TRUE(rcp_ep_mdio_word_count_of(packed.len, &word_count));
    TEST_ASSERT_EQUAL_UINT32(3, word_count);
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT16(words[i], rcp_ep_mdio_unpack_word_at(packed.data, i));
    }

    rcp_bytes_free(&packed);
}

static void test_pack_words_zero_count_returns_zeroed(void)
{
    rcp_bytes_t packed = rcp_ep_mdio_pack_words(NULL, 0);

    TEST_ASSERT_NULL(packed.data);
    TEST_ASSERT_EQUAL_UINT32(0, packed.len);
}

static void test_word_count_of_rejects_odd_length(void)
{
    size_t word_count = 0;

    TEST_ASSERT_FALSE(rcp_ep_mdio_word_count_of(3, &word_count));
}

static void test_word_count_of_accepts_even_length(void)
{
    size_t word_count = 0;

    TEST_ASSERT_TRUE(rcp_ep_mdio_word_count_of(8, &word_count));
    TEST_ASSERT_EQUAL_UINT32(4, word_count);
}

/* ── Functional config ─────────────────────────────────────────────────────── */

static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;

    memset(&cfg, 0xFF, sizeof(cfg));
    rcp_ep_mdio_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(
        rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream(void)
{
    rcp_lifecycle_writer_ctx_t none          = {0};
    rcp_lifecycle_writer_ctx_t via_ep0       = {0};
    rcp_lifecycle_writer_ctx_t via_stream    = {0};
    rcp_lifecycle_writer_ctx_t via_discovery = {0};

    via_ep0.via_root_client_ep0        = true;
    via_stream.via_owning_stream       = true;
    via_discovery.via_discovery_stream = true;

    /* REQ-LIFECYCLE-030/036: HW_CONFIGURED functional-config write access
     * now requires the root client via EP0, the endpoint's own owning
     * stream, or the discovery stream -- no longer any writer
     * unconditionally. */
    TEST_ASSERT_FALSE(rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none       = {0};
    rcp_lifecycle_writer_ctx_t via_ep0    = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0  = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(
        rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(
        rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(
        rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_mdio_errc_t codes[] = {
        RCP_EP_MDIO_OK,
        RCP_EP_MDIO_ERR_SHORT_FRAME,
        RCP_EP_MDIO_ERR_BAD_MSG_TYPE,
        RCP_EP_MDIO_ERR_WRONG_BUS,
        RCP_EP_MDIO_ERR_WRONG_OP,
        RCP_EP_MDIO_ERR_BAD_ADDR,
        RCP_EP_MDIO_ERR_BAD_WORD_COUNT,
        RCP_EP_MDIO_ERR_ALLOC,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_mdio_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_mdio_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_mdio_strerror((rcp_ep_mdio_errc_t)999));
}

/* ── Read request round trip ───────────────────────────────────────────────── */

static void test_read_request_round_trip_single_word(void)
{
    rcp_ep_mdio_addr_t addr = clause45_addr(3, 1, 0x1234);
    rcp_bytes_t         frame = rcp_ep_mdio_encode_read_request(6, addr, 1, 9);
    rcp_ep_mdio_addr_t  out_addr;
    size_t              out_word_count = 0;
    uint8_t             txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 6, &out_addr, &out_word_count, &txn));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_CLAUSE_45, out_addr.clause);
    TEST_ASSERT_EQUAL_UINT8(3, out_addr.prtad);
    TEST_ASSERT_EQUAL_UINT8(1, out_addr.devad);
    TEST_ASSERT_EQUAL_UINT16(0x1234, out_addr.regad);
    TEST_ASSERT_EQUAL_UINT32(1, out_word_count);
    TEST_ASSERT_EQUAL_UINT8(9, txn);

    rcp_bytes_free(&frame);
}

static void test_read_request_round_trip_burst(void)
{
    rcp_ep_mdio_addr_t addr = clause22_addr(2, 5);
    rcp_bytes_t         frame = rcp_ep_mdio_encode_read_request(4, addr, 16, 1);
    rcp_ep_mdio_addr_t  out_addr;
    size_t              out_word_count = 0;
    uint8_t             txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 4, &out_addr, &out_word_count, &txn));
    TEST_ASSERT_EQUAL_UINT32(16, out_word_count);

    rcp_bytes_free(&frame);
}

static void test_read_request_encode_rejects_invalid_addr(void)
{
    rcp_ep_mdio_addr_t addr   = clause22_addr(0, 0);
    rcp_bytes_t         frame;

    addr.devad = 1; /* invalid for Clause-22 */
    frame      = rcp_ep_mdio_encode_read_request(1, addr, 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_read_request_encode_rejects_zero_word_count(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_read_request(1, clause22_addr(0, 0), 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_read_request_encode_rejects_word_count_above_max(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_read_request(
        1, clause22_addr(0, 0), RCP_EP_MDIO_MAX_BURST_WORDS + 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_read_request_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t         frame = rcp_ep_mdio_encode_read_request(4, clause22_addr(0, 0), 1, 0);
    rcp_ep_mdio_addr_t  out_addr;
    size_t              out_word_count;
    uint8_t             txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_BUS, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 5, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_decode_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE; /* not a read request */
    frame           = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_OP, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain MDIO read request; every other value (here, 0b100, reserved in
 * MDIO's endpoint-type row) shall be rejected. */
static void test_read_request_decode_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 0x4;
    frame           = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_EVT, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_decode_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    rcp_ep_mdio_addr_t   out_addr;
    size_t               out_word_count;
    uint8_t              txn;

    gbb_hdr.info.byte_bus_id = 2;
    gbb_hdr.info.op          = RCP_ACF_OP_READ;
    frame                    = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_MSG_TYPE, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_decode_rejects_short_frame(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     too_short[3] = {0, 0, 0}; /* < 7-byte prefix */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, too_short, sizeof(too_short));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_SHORT_FRAME, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_decode_rejects_bad_addr(void)
{
    rcp_acf_byte_message_info_t hdr     = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[7] = {0}; /* clause=0 (Clause-22) */

    payload[0] = 0;    /* clause */
    payload[1] = 0;    /* prtad */
    payload[2] = 1;    /* devad -- invalid for Clause-22 */
    payload[3] = 0;
    payload[4] = 0;    /* regad */
    payload[5] = 0;
    payload[6] = 1;    /* word_count */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_ADDR, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_request_decode_rejects_zero_word_count(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[7] = {0}; /* word_count field left 0 */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* ── Read response round trip ──────────────────────────────────────────────── */

static void test_read_response_round_trip_untimed(void)
{
    uint16_t       words[2] = {0x1111, 0x2222};
    rcp_bytes_t    frame    = rcp_ep_mdio_encode_read_response(3, words, 2, 5, false, 0);
    const uint8_t  *out_words_data = NULL;
    size_t          out_word_count = 0;
    bool            timed = true;
    uint64_t        ts = 1;
    uint8_t         txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_read_response(
        frame.data, frame.len, 3, &out_words_data, &out_word_count, &timed, &ts, &txn));

    TEST_ASSERT_EQUAL_UINT32(2, out_word_count);
    TEST_ASSERT_EQUAL_UINT16(0x1111, rcp_ep_mdio_unpack_word_at(out_words_data, 0));
    TEST_ASSERT_EQUAL_UINT16(0x2222, rcp_ep_mdio_unpack_word_at(out_words_data, 1));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    TEST_ASSERT_EQUAL_UINT8(5, txn);

    rcp_bytes_free(&frame);
}

static void test_read_response_round_trip_timed(void)
{
    uint16_t       words[1] = {0xABCD};
    rcp_bytes_t    frame    = rcp_ep_mdio_encode_read_response(3, words, 1, 2, true, 424242);
    const uint8_t  *out_words_data = NULL;
    size_t          out_word_count = 0;
    bool            timed = false;
    uint64_t        ts = 0;
    uint8_t         txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_read_response(
        frame.data, frame.len, 3, &out_words_data, &out_word_count, &timed, &ts, &txn));

    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(424242, ts);

    rcp_bytes_free(&frame);
}

static void test_read_response_empty_words(void)
{
    rcp_bytes_t    frame    = rcp_ep_mdio_encode_read_response(3, NULL, 0, 2, false, 0);
    const uint8_t  *out_words_data = NULL;
    size_t          out_word_count = 1;
    bool            timed = false;
    uint64_t        ts = 0;
    uint8_t         txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_read_response(
        frame.data, frame.len, 3, &out_words_data, &out_word_count, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_word_count);

    rcp_bytes_free(&frame);
}

static void test_read_response_decode_rejects_wrong_bus(void)
{
    uint16_t       words[1] = {1};
    rcp_bytes_t    frame    = rcp_ep_mdio_encode_read_response(3, words, 1, 0, false, 0);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_BUS, rcp_ep_mdio_decode_read_response(
        frame.data, frame.len, 4, &out_words_data, &out_word_count, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_read_response_decode_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0x0E, 0, 0};
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_SHORT_FRAME, rcp_ep_mdio_decode_read_response(
        too_short, sizeof(too_short), 2, &out_words_data, &out_word_count, &timed, &ts, &txn));
}

static void test_read_response_decode_rejects_odd_payload_length(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    bool                          timed;
    uint64_t                      ts;
    uint8_t                       txn;
    uint8_t                       odd_payload[3] = {0, 0, 0};

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, odd_payload, sizeof(odd_payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_read_response(
        frame.data, frame.len, 2, &out_words_data, &out_word_count, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

/* ── Write request round trip ──────────────────────────────────────────────── */

static void test_write_request_round_trip_single_word(void)
{
    rcp_ep_mdio_addr_t addr        = clause45_addr(4, 2, 0x0010);
    uint16_t             words[1]  = {0x9999};
    rcp_bytes_t          frame     = rcp_ep_mdio_encode_write_request(7, addr, words, 1, 3);
    rcp_ep_mdio_addr_t    out_addr;
    const uint8_t         *out_words_data = NULL;
    size_t                 out_word_count = 0;
    uint8_t                txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 7, &out_addr, &out_words_data, &out_word_count, &txn));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_CLAUSE_45, out_addr.clause);
    TEST_ASSERT_EQUAL_UINT8(4, out_addr.prtad);
    TEST_ASSERT_EQUAL_UINT8(2, out_addr.devad);
    TEST_ASSERT_EQUAL_UINT16(0x0010, out_addr.regad);
    TEST_ASSERT_EQUAL_UINT32(1, out_word_count);
    TEST_ASSERT_EQUAL_UINT16(0x9999, rcp_ep_mdio_unpack_word_at(out_words_data, 0));
    TEST_ASSERT_EQUAL_UINT8(3, txn);

    rcp_bytes_free(&frame);
}

static void test_write_request_round_trip_burst(void)
{
    rcp_ep_mdio_addr_t addr       = clause22_addr(1, 0);
    uint16_t             words[4] = {1, 2, 3, 4};
    rcp_bytes_t          frame    = rcp_ep_mdio_encode_write_request(5, addr, words, 4, 0);
    rcp_ep_mdio_addr_t    out_addr;
    const uint8_t         *out_words_data = NULL;
    size_t                 out_word_count = 0;
    uint8_t                txn = 0;
    size_t                 i;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 5, &out_addr, &out_words_data, &out_word_count, &txn));
    TEST_ASSERT_EQUAL_UINT32(4, out_word_count);
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_UINT16(words[i], rcp_ep_mdio_unpack_word_at(out_words_data, i));
    }

    rcp_bytes_free(&frame);
}

static void test_write_request_encode_rejects_invalid_addr(void)
{
    rcp_ep_mdio_addr_t addr    = clause22_addr(0, 0x20); /* regad out of range */
    uint16_t             word   = 1;
    rcp_bytes_t          frame  = rcp_ep_mdio_encode_write_request(1, addr, &word, 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_write_request_encode_rejects_zero_word_count(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_write_request(1, clause22_addr(0, 0), NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_write_request_encode_rejects_word_count_above_max(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_write_request(
        1, clause22_addr(0, 0), NULL, RCP_EP_MDIO_MAX_BURST_WORDS + 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

static void test_write_request_decode_rejects_wrong_bus(void)
{
    uint16_t              word  = 1;
    rcp_bytes_t           frame = rcp_ep_mdio_encode_write_request(
        4, clause22_addr(0, 0), &word, 1, 0);
    rcp_ep_mdio_addr_t    out_addr;
    const uint8_t         *out_words_data;
    size_t                 out_word_count;
    uint8_t                txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_BUS, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 5, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_write_request_decode_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ; /* not a write request */
    frame           = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_OP, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 30: evt[2:0] = 000b is the only legal value for a
 * plain MDIO write request; every other value (here, 0b001, reserved in
 * MDIO's endpoint-type row) shall be rejected. */
static void test_write_request_decode_rejects_nonzero_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    hdr.evt         = 0x1;
    frame           = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_EVT, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_write_request_decode_rejects_short_frame(void)
{
    rcp_acf_byte_message_info_t hdr          = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       too_short[2] = {0, 0}; /* < 5-byte address prefix */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, too_short, sizeof(too_short));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_SHORT_FRAME, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_write_request_decode_rejects_bad_addr(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       payload[5 + 2] = {0}; /* clause=0, devad=1 (invalid), + 1 word */

    payload[2] = 1; /* devad -- invalid for Clause-22 */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_ADDR, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

static void test_write_request_decode_rejects_zero_words(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       payload[5] = {0}; /* address prefix only, no words */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* ── Write response round trip ─────────────────────────────────────────────── */

static void test_write_response_round_trip_untimed(void)
{
    uint16_t       accepted[2] = {0xAAAA, 0xBBBB};
    rcp_bytes_t    frame       = rcp_ep_mdio_encode_write_response(3, accepted, 2, 6, false, 0);
    const uint8_t  *out_words_data = NULL;
    size_t          out_word_count = 0;
    bool            timed = true;
    uint64_t        ts = 1;
    uint8_t         txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_write_response(
        frame.data, frame.len, 3, &out_words_data, &out_word_count, &timed, &ts, &txn));

    TEST_ASSERT_EQUAL_UINT32(2, out_word_count);
    TEST_ASSERT_EQUAL_UINT16(0xAAAA, rcp_ep_mdio_unpack_word_at(out_words_data, 0));
    TEST_ASSERT_EQUAL_UINT16(0xBBBB, rcp_ep_mdio_unpack_word_at(out_words_data, 1));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT8(6, txn);

    rcp_bytes_free(&frame);
}

static void test_write_response_round_trip_timed(void)
{
    uint16_t       accepted[1] = {0x1};
    rcp_bytes_t    frame       = rcp_ep_mdio_encode_write_response(3, accepted, 1, 1, true, 55);
    const uint8_t  *out_words_data = NULL;
    size_t          out_word_count = 0;
    bool            timed = false;
    uint64_t        ts = 0;
    uint8_t         txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_write_response(
        frame.data, frame.len, 3, &out_words_data, &out_word_count, &timed, &ts, &txn));

    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(55, ts);

    rcp_bytes_free(&frame);
}

static void test_write_response_nothing_accepted(void)
{
    rcp_bytes_t    frame = rcp_ep_mdio_encode_write_response(3, NULL, 0, 6, false, 0);
    const uint8_t  *out_words_data = NULL;
    size_t          out_word_count = 1;
    bool            timed = false;
    uint64_t        ts = 0;
    uint8_t         txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_write_response(
        frame.data, frame.len, 3, &out_words_data, &out_word_count, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_word_count);

    rcp_bytes_free(&frame);
}

static void test_write_response_decode_rejects_wrong_bus(void)
{
    uint16_t       accepted[1] = {1};
    rcp_bytes_t    frame       = rcp_ep_mdio_encode_write_response(3, accepted, 1, 0, false, 0);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_BUS, rcp_ep_mdio_decode_write_response(
        frame.data, frame.len, 9, &out_words_data, &out_word_count, &timed, &ts, &txn));

    rcp_bytes_free(&frame);
}

static void test_write_response_decode_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0x0E, 0, 0};
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_SHORT_FRAME, rcp_ep_mdio_decode_write_response(
        too_short, sizeof(too_short), 2, &out_words_data, &out_word_count, &timed, &ts, &txn));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_addr_valid_clause22_in_range);
    RUN_TEST(test_addr_valid_clause22_rejects_nonzero_devad);
    RUN_TEST(test_addr_valid_clause22_rejects_regad_above_5_bits);
    RUN_TEST(test_addr_valid_clause45_in_range);
    RUN_TEST(test_addr_valid_clause45_rejects_devad_above_5_bits);
    RUN_TEST(test_addr_valid_rejects_prtad_above_5_bits_either_clause);
    RUN_TEST(test_addr_valid_rejects_unknown_clause);

    RUN_TEST(test_burst_next_regad_clause22_increments);
    RUN_TEST(test_burst_next_regad_clause22_wraps_at_5_bits);
    RUN_TEST(test_burst_next_regad_clause45_increments);
    RUN_TEST(test_burst_next_regad_clause45_wraps_at_16_bits);
    RUN_TEST(test_burst_next_regad_unknown_clause_unchanged);

    RUN_TEST(test_word_encode_decode_round_trip);
    RUN_TEST(test_word_encode_is_big_endian);
    RUN_TEST(test_pack_len);
    RUN_TEST(test_pack_words_round_trip);
    RUN_TEST(test_pack_words_zero_count_returns_zeroed);
    RUN_TEST(test_word_count_of_rejects_odd_length);
    RUN_TEST(test_word_count_of_accepts_even_length);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_read_request_round_trip_single_word);
    RUN_TEST(test_read_request_round_trip_burst);
    RUN_TEST(test_read_request_encode_rejects_invalid_addr);
    RUN_TEST(test_read_request_encode_rejects_zero_word_count);
    RUN_TEST(test_read_request_encode_rejects_word_count_above_max);
    RUN_TEST(test_read_request_decode_rejects_wrong_bus);
    RUN_TEST(test_read_request_decode_rejects_wrong_op);
    RUN_TEST(test_read_request_decode_rejects_nonzero_evt);
    RUN_TEST(test_read_request_decode_rejects_bad_msg_type);
    RUN_TEST(test_read_request_decode_rejects_short_frame);
    RUN_TEST(test_read_request_decode_rejects_bad_addr);
    RUN_TEST(test_read_request_decode_rejects_zero_word_count);

    RUN_TEST(test_read_response_round_trip_untimed);
    RUN_TEST(test_read_response_round_trip_timed);
    RUN_TEST(test_read_response_empty_words);
    RUN_TEST(test_read_response_decode_rejects_wrong_bus);
    RUN_TEST(test_read_response_decode_rejects_short_frame);
    RUN_TEST(test_read_response_decode_rejects_odd_payload_length);

    RUN_TEST(test_write_request_round_trip_single_word);
    RUN_TEST(test_write_request_round_trip_burst);
    RUN_TEST(test_write_request_encode_rejects_invalid_addr);
    RUN_TEST(test_write_request_encode_rejects_zero_word_count);
    RUN_TEST(test_write_request_encode_rejects_word_count_above_max);
    RUN_TEST(test_write_request_decode_rejects_wrong_bus);
    RUN_TEST(test_write_request_decode_rejects_wrong_op);
    RUN_TEST(test_write_request_decode_rejects_nonzero_evt);
    RUN_TEST(test_write_request_decode_rejects_short_frame);
    RUN_TEST(test_write_request_decode_rejects_bad_addr);
    RUN_TEST(test_write_request_decode_rejects_zero_words);

    RUN_TEST(test_write_response_round_trip_untimed);
    RUN_TEST(test_write_response_round_trip_timed);
    RUN_TEST(test_write_response_nothing_accepted);
    RUN_TEST(test_write_response_decode_rejects_wrong_bus);
    RUN_TEST(test_write_response_decode_rejects_short_frame);

    return UNITY_END();
}
