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
 * dangling-reference noise this rewrite avoids.) */
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

static rcp_ep_mdio_mms_addr_t mms_addr(uint8_t mms, uint16_t addr_val)
{
    rcp_ep_mdio_mms_addr_t addr;

    addr.mms  = mms;
    addr.addr = addr_val;
    return addr;
}

/* ── addr_valid ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-001
static void test_addr_valid_clause22_in_range(void)
{
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause22_addr(0, 0)));
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause22_addr(0x1F, 0x1F)));
}

//cfusa:test REQ-MDIO-001
static void test_addr_valid_clause22_rejects_nonzero_devad(void)
{
    rcp_ep_mdio_addr_t addr = clause22_addr(1, 1);

    addr.devad = 1;
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(addr));
}

//cfusa:test REQ-MDIO-001
static void test_addr_valid_clause22_rejects_regad_above_5_bits(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause22_addr(0, 0x20)));
}

//cfusa:test REQ-MDIO-001
static void test_addr_valid_clause45_in_range(void)
{
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause45_addr(0, 0, 0)));
    TEST_ASSERT_TRUE(rcp_ep_mdio_addr_valid(clause45_addr(0x1F, 0x1F, 0xFFFF)));
}

//cfusa:test REQ-MDIO-001
static void test_addr_valid_clause45_rejects_devad_above_5_bits(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause45_addr(0, 0x20, 0)));
}

//cfusa:test REQ-MDIO-001
static void test_addr_valid_rejects_prtad_above_5_bits_either_clause(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause22_addr(0x20, 0)));
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(clause45_addr(0x20, 0, 0)));
}

//cfusa:test REQ-MDIO-001
static void test_addr_valid_rejects_unknown_clause(void)
{
    rcp_ep_mdio_addr_t addr = clause22_addr(0, 0);

    addr.clause = (rcp_ep_mdio_clause_t)2;
    TEST_ASSERT_FALSE(rcp_ep_mdio_addr_valid(addr));
}

/* ── burst_next_regad ──────────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-002
static void test_burst_next_regad_clause22_increments(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_22, 0));
}

//cfusa:test REQ-MDIO-002
static void test_burst_next_regad_clause22_wraps_at_5_bits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_22, 0x1F));
}

//cfusa:test REQ-MDIO-002
static void test_burst_next_regad_clause45_increments(void)
{
    TEST_ASSERT_EQUAL_UINT16(0x1235, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_45, 0x1234));
}

//cfusa:test REQ-MDIO-002
static void test_burst_next_regad_clause45_wraps_at_16_bits(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, rcp_ep_mdio_burst_next_regad(RCP_EP_MDIO_CLAUSE_45, 0xFFFF));
}

//cfusa:test REQ-MDIO-002
static void test_burst_next_regad_unknown_clause_unchanged(void)
{
    TEST_ASSERT_EQUAL_UINT16(42, rcp_ep_mdio_burst_next_regad((rcp_ep_mdio_clause_t)2, 42));
}

/* ── mdio_mode: REQ-MDIO-021 ────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-021
static void test_mode_for_word_count_single(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_MODE_MMD_SINGLE, rcp_ep_mdio_mode_for_word_count(1));
}

//cfusa:test REQ-MDIO-021
static void test_mode_for_word_count_multi(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_MODE_MMD_MULTI, rcp_ep_mdio_mode_for_word_count(2));
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_MODE_MMD_MULTI,
                      rcp_ep_mdio_mode_for_word_count(RCP_EP_MDIO_MAX_BURST_WORDS));
}

//cfusa:test REQ-MDIO-021
static void test_mode_is_unsupported_mms(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_mode_is_unsupported_mms(RCP_EP_MDIO_MODE_MMD_SINGLE));
    TEST_ASSERT_FALSE(rcp_ep_mdio_mode_is_unsupported_mms(RCP_EP_MDIO_MODE_MMD_MULTI));
    TEST_ASSERT_TRUE(rcp_ep_mdio_mode_is_unsupported_mms(RCP_EP_MDIO_MODE_MMS_SINGLE));
    TEST_ASSERT_TRUE(rcp_ep_mdio_mode_is_unsupported_mms(RCP_EP_MDIO_MODE_MMS_MULTI));
}

/* REQ-MDIO-021: a read request's own encoded mdio_mode octet (the byte
 * immediately after the 8-byte ACF_ABB header) reflects word_count's own
 * single-vs-multi distinction. */
//cfusa:test REQ-MDIO-021
static void test_read_request_encode_sets_mdio_mode_single(void)
{
    rcp_ep_mdio_addr_t addr  = clause22_addr(1, 0);
    rcp_bytes_t         frame = rcp_ep_mdio_encode_read_request(2, addr, 1, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len > RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE,
                            frame.data[RCP_ACF_ABB_HEADER_LEN]);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-021
static void test_read_request_encode_sets_mdio_mode_multi(void)
{
    rcp_ep_mdio_addr_t addr  = clause22_addr(1, 0);
    rcp_bytes_t         frame = rcp_ep_mdio_encode_read_request(2, addr, 4, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len > RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_MODE_MMD_MULTI,
                            frame.data[RCP_ACF_ABB_HEADER_LEN]);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-021
static void test_write_request_encode_sets_mdio_mode_single(void)
{
    rcp_ep_mdio_addr_t addr    = clause45_addr(1, 2, 0);
    uint16_t             word   = 0x1234;
    rcp_bytes_t          frame  = rcp_ep_mdio_encode_write_request(2, addr, &word, 1, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len > RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE,
                            frame.data[RCP_ACF_ABB_HEADER_LEN]);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-021
static void test_write_request_encode_sets_mdio_mode_multi(void)
{
    rcp_ep_mdio_addr_t addr       = clause45_addr(1, 2, 0);
    uint16_t             words[3]  = {1, 2, 3};
    rcp_bytes_t          frame     = rcp_ep_mdio_encode_write_request(2, addr, words, 3, 0);

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_TRUE(frame.len > RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_MODE_MMD_MULTI,
                            frame.data[RCP_ACF_ABB_HEADER_LEN]);

    rcp_bytes_free(&frame);
}

/* REQ-MDIO-021's own still-open remainder: a request whose mdio_mode
 * octet decodes to an MMS value is recognized on the wire but rejected,
 * not silently misread as if it were MMD-shaped -- see the file header's
 * own "mdio_mode" section. */
//cfusa:test REQ-MDIO-013
//cfusa:test REQ-MDIO-021
static void test_read_request_decode_rejects_mms_mode(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[8] = {0};

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMS_SINGLE;
    payload[7] = 1; /* word_count -- otherwise-valid request */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_UNSUPPORTED_MMS, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-017
//cfusa:test REQ-MDIO-021
static void test_write_request_decode_rejects_mms_mode(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       payload[1 + 5 + 2] = {0}; /* otherwise-valid, 1 word */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMS_MULTI;

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_UNSUPPORTED_MMS, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* ── Register-word packing ─────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-003
//cfusa:test REQ-MDIO-004
static void test_word_encode_decode_round_trip(void)
{
    uint8_t buf[2];

    rcp_ep_mdio_word_encode(0xBEEF, buf);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, rcp_ep_mdio_word_decode(buf));
}

//cfusa:test REQ-MDIO-003
static void test_word_encode_is_big_endian(void)
{
    uint8_t buf[2];

    rcp_ep_mdio_word_encode(0x1234, buf);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[1]);
}

//cfusa:test REQ-MDIO-005
static void test_pack_len(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, rcp_ep_mdio_pack_len(0));
    TEST_ASSERT_EQUAL_UINT32(6, rcp_ep_mdio_pack_len(3));
}

//cfusa:test REQ-MDIO-006
//cfusa:test REQ-MDIO-008
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

//cfusa:test REQ-MDIO-006
static void test_pack_words_zero_count_returns_zeroed(void)
{
    rcp_bytes_t packed = rcp_ep_mdio_pack_words(NULL, 0);

    TEST_ASSERT_NULL(packed.data);
    TEST_ASSERT_EQUAL_UINT32(0, packed.len);
}

//cfusa:test REQ-MDIO-007
static void test_word_count_of_rejects_odd_length(void)
{
    size_t word_count = 0;

    TEST_ASSERT_FALSE(rcp_ep_mdio_word_count_of(3, &word_count));
}

//cfusa:test REQ-MDIO-007
static void test_word_count_of_accepts_even_length(void)
{
    size_t word_count = 0;

    TEST_ASSERT_TRUE(rcp_ep_mdio_word_count_of(8, &word_count));
    TEST_ASSERT_EQUAL_UINT32(4, word_count);
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-009
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
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
}

//cfusa:test REQ-MDIO-010
static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(
        rcp_ep_mdio_functional_cfg_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

//cfusa:test REQ-MDIO-010
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

//cfusa:test REQ-MDIO-010
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

/* ── The EP_func register block ──────────────────────────────────────────── */

//cfusa:test REQ-MDIO-020
//cfusa:test REQ-MDIO-023
static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;
    uint8_t                      out[RCP_EP_MDIO_EP_FUNC_LEN];

    rcp_ep_mdio_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status          = 0x1234;

    rcp_ep_mdio_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_EP_FUNC_LEN, out[RCP_EP_MDIO_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_MDIO_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_MDIO_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_MDIO_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_MDIO_REG_EP_STATUS + 1]);

    TEST_ASSERT_EQUAL_UINT16(0x0006u, RCP_EP_MDIO_EP_FUNC_LEN);
}

//cfusa:test REQ-MDIO-023
static void test_apply_reconfig_writes_ep_status(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;
    uint8_t                      payload[2 + 2];

    rcp_ep_mdio_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_MDIO_REG_EP_STATUS;
    payload[2] = 0xAB; payload[3] = 0xCD;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_RECONFIG_OK,
        rcp_ep_mdio_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
}

//cfusa:test REQ-MDIO-023
static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;
    uint8_t                      payload[2 + 2];

    rcp_ep_mdio_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00) and the reserved octet (0x01) -- both read-only.
     * No base_clk row exists here, unlike every other endpoint type's own
     * common prefix -- see the file header. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_RECONFIG_OK,
        rcp_ep_mdio_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_MDIO_EP_FUNC_LEN];

        rcp_ep_mdio_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_MDIO_EP_FUNC_LEN, out[RCP_EP_MDIO_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_MDIO_REG_RESERVED_01]);
    }
}

//cfusa:test REQ-MDIO-023
static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;
    uint8_t                      payload[3];

    rcp_ep_mdio_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x06; /* == RCP_EP_MDIO_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_mdio_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
}

//cfusa:test REQ-MDIO-023
static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_mdio_functional_cfg_t cfg;
    uint8_t                      addr_only[2] = {0x00, 0x04};

    rcp_ep_mdio_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_RECONFIG_ERR_SHORT,
        rcp_ep_mdio_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_RECONFIG_ERR_SHORT,
        rcp_ep_mdio_apply_reconfig(&cfg, NULL, 0));
}

//cfusa:test REQ-MDIO-023
static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_mdio_encode_reconfig_request(0x03, 0x0004, data, sizeof(data), 7);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x03, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL(RCP_ACF_OP_WRITE, hdr.op);
    TEST_ASSERT_EQUAL_UINT8(0x7u, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(7, hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT32(4, payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, payload[3]);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-023
static void test_encode_reconfig_request_rejects_empty_data(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-023
static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_mdio_reconfig_errc_t codes[] = {
        RCP_EP_MDIO_RECONFIG_OK, RCP_EP_MDIO_RECONFIG_ERR_SHORT,
        RCP_EP_MDIO_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_mdio_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_mdio_reconfig_strerror((rcp_ep_mdio_reconfig_errc_t)99));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-011
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

//cfusa:test REQ-MDIO-012
//cfusa:test REQ-MDIO-013
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

//cfusa:test REQ-MDIO-012
//cfusa:test REQ-MDIO-013
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

//cfusa:test REQ-MDIO-012
static void test_read_request_encode_rejects_invalid_addr(void)
{
    rcp_ep_mdio_addr_t addr   = clause22_addr(0, 0);
    rcp_bytes_t         frame;

    addr.devad = 1; /* invalid for Clause-22 */
    frame      = rcp_ep_mdio_encode_read_request(1, addr, 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-012
static void test_read_request_encode_rejects_zero_word_count(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_read_request(1, clause22_addr(0, 0), 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-012
static void test_read_request_encode_rejects_word_count_above_max(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_read_request(
        1, clause22_addr(0, 0), RCP_EP_MDIO_MAX_BURST_WORDS + 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-013
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

//cfusa:test REQ-MDIO-013
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
//cfusa:test REQ-MDIO-013
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

//cfusa:test REQ-MDIO-013
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

//cfusa:test REQ-MDIO-013
static void test_read_request_decode_rejects_short_frame(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     too_short[3] = {0, 0, 0}; /* < 8-byte (mode+address) prefix */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, too_short, sizeof(too_short));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_SHORT_FRAME, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-013
static void test_read_request_decode_rejects_bad_addr(void)
{
    rcp_acf_byte_message_info_t hdr     = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[8] = {0}; /* mdio_mode(1) + clause=0 (Clause-22) */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE; /* mdio_mode */
    payload[1] = 0;    /* clause */
    payload[2] = 0;    /* prtad */
    payload[3] = 1;    /* devad -- invalid for Clause-22 */
    payload[4] = 0;
    payload[5] = 0;    /* regad */
    payload[6] = 0;
    payload[7] = 1;    /* word_count */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_ADDR, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-013
static void test_read_request_decode_rejects_zero_word_count(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[8] = {0}; /* mdio_mode(1) + address(5) + word_count field left 0 */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE; /* mdio_mode */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_read_request(
        frame.data, frame.len, 2, &out_addr, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* ── Read response round trip ──────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-014
//cfusa:test REQ-MDIO-015
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

//cfusa:test REQ-MDIO-025
//cfusa:test REQ-MDIO-026
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

//cfusa:test REQ-MDIO-014
//cfusa:test REQ-MDIO-015
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

//cfusa:test REQ-MDIO-015
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

//cfusa:test REQ-MDIO-015
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

//cfusa:test REQ-MDIO-015
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

//cfusa:test REQ-MDIO-016
//cfusa:test REQ-MDIO-017
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

//cfusa:test REQ-MDIO-016
//cfusa:test REQ-MDIO-017
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

//cfusa:test REQ-MDIO-016
static void test_write_request_encode_rejects_invalid_addr(void)
{
    rcp_ep_mdio_addr_t addr    = clause22_addr(0, 0x20); /* regad out of range */
    uint16_t             word   = 1;
    rcp_bytes_t          frame  = rcp_ep_mdio_encode_write_request(1, addr, &word, 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-016
static void test_write_request_encode_rejects_zero_word_count(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_write_request(1, clause22_addr(0, 0), NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-016
static void test_write_request_encode_rejects_word_count_above_max(void)
{
    rcp_bytes_t frame = rcp_ep_mdio_encode_write_request(
        1, clause22_addr(0, 0), NULL, RCP_EP_MDIO_MAX_BURST_WORDS + 1, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-MDIO-017
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

//cfusa:test REQ-MDIO-017
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
//cfusa:test REQ-MDIO-017
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

//cfusa:test REQ-MDIO-017
static void test_write_request_decode_rejects_short_frame(void)
{
    rcp_acf_byte_message_info_t hdr          = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       too_short[2] = {0, 0}; /* < 6-byte (mode+address) prefix */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, too_short, sizeof(too_short));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_SHORT_FRAME, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-017
static void test_write_request_decode_rejects_bad_addr(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    /* mdio_mode(1) + clause=0, devad=1 (invalid), + 1 word */
    uint8_t                       payload[1 + 5 + 2] = {0};

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE; /* mdio_mode */
    payload[3] = 1; /* devad -- invalid for Clause-22 */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_ADDR, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-017
static void test_write_request_decode_rejects_zero_words(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_addr_t          out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       payload[1 + 5] = {0}; /* mdio_mode(1) + address prefix only, no words */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE; /* mdio_mode */

    hdr.byte_bus_id = 2;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_write_request(
        frame.data, frame.len, 2, &out_addr, &out_words_data, &out_word_count, &txn));

    rcp_bytes_free(&frame);
}

/* ── Write response round trip ─────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-018
//cfusa:test REQ-MDIO-019
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

//cfusa:test REQ-MDIO-027
//cfusa:test REQ-MDIO-028
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

//cfusa:test REQ-MDIO-018
//cfusa:test REQ-MDIO-019
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

//cfusa:test REQ-MDIO-019
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

//cfusa:test REQ-MDIO-019
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


/* ── MMS addressing: REQ-MDIO-022/024 ─────────────────────────────────────────
 *
 * See ep_mdio.h's own "MMS addressing" section for the documented,
 * externally-sourced (OPEN Alliance 10BASE-T1x SPI spec) assumption this
 * whole section pins: mdio_address = 4-bit MMS selector (its own whole
 * octet on this module's wire) + 16-bit ADDR. The 32-vs-16-bit word width
 * rule itself (mms 0/1 -> 32-bit, else 16-bit) is TC18-literal, not an
 * assumption -- see rcp_ep_mdio_mms_uses_32bit_words(). */

//cfusa:test REQ-MDIO-024
static void test_mms_addr_valid_in_range(void)
{
    TEST_ASSERT_TRUE(rcp_ep_mdio_mms_addr_valid(mms_addr(0, 0)));
    TEST_ASSERT_TRUE(rcp_ep_mdio_mms_addr_valid(mms_addr(RCP_EP_MDIO_MMS_MAX, 0xFFFFu)));
}

//cfusa:test REQ-MDIO-024
static void test_mms_addr_valid_rejects_above_max(void)
{
    TEST_ASSERT_FALSE(rcp_ep_mdio_mms_addr_valid(mms_addr((uint8_t)(RCP_EP_MDIO_MMS_MAX + 1u), 0)));
}

//cfusa:test REQ-MDIO-022
static void test_mms_uses_32bit_words_mms0_and_mms1(void)
{
    TEST_ASSERT_TRUE(rcp_ep_mdio_mms_uses_32bit_words(0));
    TEST_ASSERT_TRUE(rcp_ep_mdio_mms_uses_32bit_words(1));
}

//cfusa:test REQ-MDIO-022
static void test_mms_uses_32bit_words_false_for_other_mms(void)
{
    uint8_t mms;

    for (mms = 2; mms <= RCP_EP_MDIO_MMS_MAX; mms++) {
        TEST_ASSERT_FALSE(rcp_ep_mdio_mms_uses_32bit_words(mms));
    }
}

//cfusa:test REQ-MDIO-024
static void test_mms_burst_next_addr_increments(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0002u, rcp_ep_mdio_mms_burst_next_addr(0x0001u));
}

//cfusa:test REQ-MDIO-024
static void test_mms_burst_next_addr_wraps_at_16_bits(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, rcp_ep_mdio_mms_burst_next_addr(0xFFFFu));
}

//cfusa:test REQ-MDIO-022
static void test_mms_mode_for_word_count_single(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_MODE_MMS_SINGLE, rcp_ep_mdio_mms_mode_for_word_count(1));
}

//cfusa:test REQ-MDIO-022
static void test_mms_mode_for_word_count_multi(void)
{
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_MODE_MMS_MULTI, rcp_ep_mdio_mms_mode_for_word_count(2));
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_MODE_MMS_MULTI,
                      rcp_ep_mdio_mms_mode_for_word_count(RCP_EP_MDIO_MAX_BURST_WORDS));
}

/* ── MMS word32 encode/decode ─────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-022
static void test_word32_encode_decode_round_trip(void)
{
    uint8_t buf[4];

    rcp_ep_mdio_word32_encode(0xDEADBEEFu, buf);
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, rcp_ep_mdio_word32_decode(buf));
}

//cfusa:test REQ-MDIO-022
static void test_word32_encode_is_big_endian(void)
{
    uint8_t buf[4];

    rcp_ep_mdio_word32_encode(0x12345678u, buf);
    TEST_ASSERT_EQUAL_HEX8(0x12u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34u, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x56u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x78u, buf[3]);
}

/* ── MMS pack/unpack: width follows mms per REQ-MDIO-022's own TC18-literal
 * rule ─────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-022
static void test_mms_pack_len_32bit_for_mms0(void)
{
    TEST_ASSERT_EQUAL_size_t(12u, rcp_ep_mdio_mms_pack_len(0, 3u));
}

//cfusa:test REQ-MDIO-022
static void test_mms_pack_len_16bit_for_other_mms(void)
{
    TEST_ASSERT_EQUAL_size_t(6u, rcp_ep_mdio_mms_pack_len(2, 3u));
}

//cfusa:test REQ-MDIO-022
static void test_mms_pack_words_round_trip_32bit(void)
{
    const uint32_t words[2] = {0x11223344u, 0xAABBCCDDu};
    rcp_bytes_t    packed   = rcp_ep_mdio_mms_pack_words(1 /* MMS1: 32-bit */, words, 2);

    TEST_ASSERT_NOT_NULL(packed.data);
    TEST_ASSERT_EQUAL_size_t(8u, packed.len);
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, rcp_ep_mdio_mms_unpack_word_at(1, packed.data, 0));
    TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, rcp_ep_mdio_mms_unpack_word_at(1, packed.data, 1));
    rcp_bytes_free(&packed);
}

//cfusa:test REQ-MDIO-022
static void test_mms_pack_words_round_trip_16bit(void)
{
    const uint32_t words[2] = {0x1234u, 0xBEEFu};
    rcp_bytes_t    packed   = rcp_ep_mdio_mms_pack_words(4 /* not 0/1: 16-bit */, words, 2);

    TEST_ASSERT_NOT_NULL(packed.data);
    TEST_ASSERT_EQUAL_size_t(4u, packed.len);
    TEST_ASSERT_EQUAL_UINT32(0x1234u, rcp_ep_mdio_mms_unpack_word_at(4, packed.data, 0));
    TEST_ASSERT_EQUAL_UINT32(0xBEEFu, rcp_ep_mdio_mms_unpack_word_at(4, packed.data, 1));
    rcp_bytes_free(&packed);
}

//cfusa:test REQ-MDIO-022
static void test_mms_pack_words_zero_count_returns_zeroed(void)
{
    rcp_bytes_t packed = rcp_ep_mdio_mms_pack_words(0, NULL, 0);

    TEST_ASSERT_NULL(packed.data);
}

//cfusa:test REQ-MDIO-022
static void test_mms_word_count_of_32bit_rejects_non_multiple_of_four(void)
{
    size_t out;

    TEST_ASSERT_FALSE(rcp_ep_mdio_mms_word_count_of(0, 5u, &out));
    TEST_ASSERT_TRUE(rcp_ep_mdio_mms_word_count_of(0, 8u, &out));
    TEST_ASSERT_EQUAL_size_t(2u, out);
}

//cfusa:test REQ-MDIO-022
static void test_mms_word_count_of_16bit_rejects_odd_length(void)
{
    size_t out;

    TEST_ASSERT_FALSE(rcp_ep_mdio_mms_word_count_of(3, 3u, &out));
    TEST_ASSERT_TRUE(rcp_ep_mdio_mms_word_count_of(3, 4u, &out));
    TEST_ASSERT_EQUAL_size_t(2u, out);
}

/* ── MMS read request/response ────────────────────────────────────────────── */

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_read_request_round_trip_single_word_32bit(void)
{
    rcp_ep_mdio_mms_addr_t      addr = mms_addr(0, 0xBEEFu); /* MMS0: 32-bit */
    rcp_bytes_t                 f;
    rcp_ep_mdio_mms_addr_t      out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;

    f = rcp_ep_mdio_encode_mms_read_request(0x10u, addr, 1u, 7u);
    TEST_ASSERT_NOT_NULL(f.data);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_read_request(
        f.data, f.len, 0x10u, &out_addr, &out_word_count, &txn));
    TEST_ASSERT_EQUAL_UINT8(0, out_addr.mms);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, out_addr.addr);
    TEST_ASSERT_EQUAL_size_t(1u, out_word_count);
    TEST_ASSERT_EQUAL_UINT8(7u, txn);

    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_read_request_round_trip_burst_16bit(void)
{
    rcp_ep_mdio_mms_addr_t      addr = mms_addr(3, 0x0100u); /* not 0/1: 16-bit */
    rcp_bytes_t                 f;
    rcp_ep_mdio_mms_addr_t      out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;

    f = rcp_ep_mdio_encode_mms_read_request(0x10u, addr, 5u, 9u);
    TEST_ASSERT_NOT_NULL(f.data);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_read_request(
        f.data, f.len, 0x10u, &out_addr, &out_word_count, &txn));
    TEST_ASSERT_EQUAL_UINT8(3, out_addr.mms);
    TEST_ASSERT_EQUAL_HEX16(0x0100u, out_addr.addr);
    TEST_ASSERT_EQUAL_size_t(5u, out_word_count);

    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_request_encode_rejects_invalid_addr(void)
{
    rcp_ep_mdio_mms_addr_t addr = mms_addr((uint8_t)(RCP_EP_MDIO_MMS_MAX + 1u), 0);
    rcp_bytes_t             f   = rcp_ep_mdio_encode_mms_read_request(0x10u, addr, 1u, 1u);

    TEST_ASSERT_NULL(f.data);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_request_encode_rejects_zero_word_count(void)
{
    rcp_bytes_t f = rcp_ep_mdio_encode_mms_read_request(0x10u, mms_addr(0, 0), 0u, 1u);

    TEST_ASSERT_NULL(f.data);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_request_encode_rejects_word_count_above_max(void)
{
    rcp_bytes_t f = rcp_ep_mdio_encode_mms_read_request(
        0x10u, mms_addr(0, 0), RCP_EP_MDIO_MAX_BURST_WORDS + 1u, 1u);

    TEST_ASSERT_NULL(f.data);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_request_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t             f = rcp_ep_mdio_encode_mms_read_request(0x10u, mms_addr(0, 0), 1u, 1u);
    rcp_ep_mdio_mms_addr_t  out_addr;
    size_t                  out_word_count;
    uint8_t                 txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_BUS, rcp_ep_mdio_decode_mms_read_request(
        f.data, f.len, 0x11u, &out_addr, &out_word_count, &txn));
    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_request_decode_rejects_bad_mms_addr(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_mms_addr_t      out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[6] = {0};

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMS_SINGLE;
    payload[1] = (uint8_t)(RCP_EP_MDIO_MMS_MAX + 1u); /* out-of-range mms */
    payload[5] = 1; /* word_count */

    hdr.byte_bus_id = 0x10u;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_MMS_ADDR, rcp_ep_mdio_decode_mms_read_request(
        frame.data, frame.len, 0x10u, &out_addr, &out_word_count, &txn));
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_request_decode_rejects_zero_word_count(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_mms_addr_t      out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[6] = {0}; /* mdio_mode+mms+addr(2)+word_count(2)=0 */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMS_SINGLE;

    hdr.byte_bus_id = 0x10u;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_mms_read_request(
        frame.data, frame.len, 0x10u, &out_addr, &out_word_count, &txn));
    rcp_bytes_free(&frame);
}

/* Mirror image of test_mdio_decode_rejects_mms_mode_fails_closed()
 * (test_tc18_gaps_ep2.c) -- the MMS decoder must equally refuse to
 * misread an MMD-mode frame as if it were MMS-shaped. */
//cfusa:test REQ-MDIO-021
//cfusa:test REQ-MDIO-024
static void test_mms_read_request_decode_rejects_mmd_mode(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_mms_addr_t      out_addr;
    size_t                      out_word_count;
    uint8_t                     txn;
    uint8_t                     payload[6] = {0};

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE;
    payload[5] = 1;

    hdr.byte_bus_id = 0x10u;
    hdr.op          = RCP_ACF_OP_READ;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_MDIO_MODE, rcp_ep_mdio_decode_mms_read_request(
        frame.data, frame.len, 0x10u, &out_addr, &out_word_count, &txn));
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_read_response_round_trip_untimed_32bit(void)
{
    const uint32_t words[1] = {0xCAFEBABEu};
    rcp_bytes_t    f        = rcp_ep_mdio_encode_mms_read_response(0x10u, 0, words, 1, 3u, false, 0);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_read_response(
        f.data, f.len, 0x10u, 0, &out_words_data, &out_word_count, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_size_t(1u, out_word_count);
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT8(3u, txn);
    TEST_ASSERT_EQUAL_UINT32(0xCAFEBABEu, rcp_ep_mdio_mms_unpack_word_at(0, out_words_data, 0));

    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_read_response_round_trip_timed_16bit(void)
{
    const uint32_t words[1] = {0x4242u};
    rcp_bytes_t    f = rcp_ep_mdio_encode_mms_read_response(0x10u, 5, words, 1, 4u, true, 999u);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_read_response(
        f.data, f.len, 0x10u, 5, &out_words_data, &out_word_count, &timed, &ts, &txn));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(999u, ts);
    TEST_ASSERT_EQUAL_UINT32(0x4242u, rcp_ep_mdio_mms_unpack_word_at(5, out_words_data, 0));

    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-024
static void test_mms_read_response_decode_rejects_wrong_bus(void)
{
    const uint32_t words[1] = {1u};
    rcp_bytes_t    f = rcp_ep_mdio_encode_mms_read_response(0x10u, 0, words, 1, 1u, false, 0);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_BUS, rcp_ep_mdio_decode_mms_read_response(
        f.data, f.len, 0x11u, 0, &out_words_data, &out_word_count, &timed, &ts, &txn));
    rcp_bytes_free(&f);
}

/* A 32-bit-mms payload whose byte length is not a multiple of 4 must be
 * rejected -- distinct from the MMD family's own odd-length check, since
 * the modulus itself depends on which mms the caller supplies. */
//cfusa:test REQ-MDIO-022
static void test_mms_read_response_decode_rejects_bad_word_count_for_32bit_mms(void)
{
    uint8_t                     odd_payload[3] = {0x01u, 0x02u, 0x03u};
    rcp_acf_byte_message_info_t hdr            = {0};
    rcp_bytes_t                 frame;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    bool                          timed;
    uint64_t                      ts;
    uint8_t                       txn;

    hdr.byte_bus_id = 0x10u;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.rsp         = 1;
    frame           = rcp_acf_encode_abb(&hdr, odd_payload, sizeof(odd_payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_mms_read_response(
        frame.data, frame.len, 0x10u, 0 /* MMS0: 32-bit, 3 bytes is not a multiple of 4 */,
        &out_words_data, &out_word_count, &timed, &ts, &txn));
    rcp_bytes_free(&frame);
}

/* ── MMS write request/response ───────────────────────────────────────────── */

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_write_request_round_trip_single_word_32bit(void)
{
    rcp_ep_mdio_mms_addr_t addr     = mms_addr(1, 0x0010u); /* MMS1: 32-bit */
    const uint32_t          words[1] = {0x11223344u};
    rcp_bytes_t              f;
    rcp_ep_mdio_mms_addr_t   out_addr;
    const uint8_t             *out_words_data;
    size_t                     out_word_count;
    uint8_t                    txn;

    f = rcp_ep_mdio_encode_mms_write_request(0x10u, addr, words, 1, 6u);
    TEST_ASSERT_NOT_NULL(f.data);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_write_request(
        f.data, f.len, 0x10u, &out_addr, &out_words_data, &out_word_count, &txn));
    TEST_ASSERT_EQUAL_UINT8(1, out_addr.mms);
    TEST_ASSERT_EQUAL_HEX16(0x0010u, out_addr.addr);
    TEST_ASSERT_EQUAL_size_t(1u, out_word_count);
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, rcp_ep_mdio_mms_unpack_word_at(1, out_words_data, 0));

    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_write_request_round_trip_burst_16bit(void)
{
    rcp_ep_mdio_mms_addr_t addr       = mms_addr(6, 0x0200u); /* not 0/1: 16-bit */
    const uint32_t          words[3]   = {0x1111u, 0x2222u, 0x3333u};
    rcp_bytes_t              f;
    rcp_ep_mdio_mms_addr_t   out_addr;
    const uint8_t             *out_words_data;
    size_t                     out_word_count;
    uint8_t                    txn;

    f = rcp_ep_mdio_encode_mms_write_request(0x10u, addr, words, 3, 8u);
    TEST_ASSERT_NOT_NULL(f.data);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_write_request(
        f.data, f.len, 0x10u, &out_addr, &out_words_data, &out_word_count, &txn));
    TEST_ASSERT_EQUAL_size_t(3u, out_word_count);
    TEST_ASSERT_EQUAL_UINT32(0x2222u, rcp_ep_mdio_mms_unpack_word_at(6, out_words_data, 1));

    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-024
static void test_mms_write_request_encode_rejects_invalid_addr(void)
{
    rcp_ep_mdio_mms_addr_t addr     = mms_addr((uint8_t)(RCP_EP_MDIO_MMS_MAX + 1u), 0);
    const uint32_t          words[1] = {1u};
    rcp_bytes_t              f       = rcp_ep_mdio_encode_mms_write_request(0x10u, addr, words, 1, 1u);

    TEST_ASSERT_NULL(f.data);
}

//cfusa:test REQ-MDIO-024
static void test_mms_write_request_encode_rejects_zero_word_count(void)
{
    rcp_bytes_t f = rcp_ep_mdio_encode_mms_write_request(0x10u, mms_addr(0, 0), NULL, 0, 1u);

    TEST_ASSERT_NULL(f.data);
}

//cfusa:test REQ-MDIO-024
static void test_mms_write_request_decode_rejects_wrong_op(void)
{
    const uint32_t          words[1] = {1u};
    rcp_bytes_t              f = rcp_ep_mdio_encode_mms_read_request(0x10u, mms_addr(0, 0), 1u, 1u);
    rcp_ep_mdio_mms_addr_t   out_addr;
    const uint8_t             *out_words_data;
    size_t                     out_word_count;
    uint8_t                    txn;

    (void)words;
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_OP, rcp_ep_mdio_decode_mms_write_request(
        f.data, f.len, 0x10u, &out_addr, &out_words_data, &out_word_count, &txn));
    rcp_bytes_free(&f);
}

//cfusa:test REQ-MDIO-024
static void test_mms_write_request_decode_rejects_zero_words(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_mms_addr_t      out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       payload[4] = {0}; /* mdio_mode(1) + mms/addr(3), no words */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMS_SINGLE;

    hdr.byte_bus_id = 0x10u;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_BAD_WORD_COUNT, rcp_ep_mdio_decode_mms_write_request(
        frame.data, frame.len, 0x10u, &out_addr, &out_words_data, &out_word_count, &txn));
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_write_response_round_trip_untimed(void)
{
    const uint32_t accepted[1] = {0xAAAAu};
    rcp_bytes_t    f = rcp_ep_mdio_encode_mms_write_response(0x10u, 2, accepted, 1, 5u, false, 0);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_write_response(
        f.data, f.len, 0x10u, 2, &out_words_data, &out_word_count, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_size_t(1u, out_word_count);
    TEST_ASSERT_EQUAL_UINT32(0xAAAAu, rcp_ep_mdio_mms_unpack_word_at(2, out_words_data, 0));

    rcp_bytes_free(&f);
}

/* Mirror image of test_mms_read_request_decode_rejects_mmd_mode(), for
 * the write side. */
//cfusa:test REQ-MDIO-021
//cfusa:test REQ-MDIO-024
static void test_mms_write_request_decode_rejects_mmd_mode(void)
{
    rcp_acf_byte_message_info_t hdr        = {0};
    rcp_bytes_t                 frame;
    rcp_ep_mdio_mms_addr_t      out_addr;
    const uint8_t                *out_words_data;
    size_t                        out_word_count;
    uint8_t                       txn;
    uint8_t                       payload[6] = {0}; /* mdio_mode(1) + mms/addr(3) + 1 word */

    payload[0] = (uint8_t)RCP_EP_MDIO_MODE_MMD_SINGLE;

    hdr.byte_bus_id = 0x10u;
    hdr.op          = RCP_ACF_OP_WRITE;
    frame           = rcp_acf_encode_abb(&hdr, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_ERR_WRONG_MDIO_MODE, rcp_ep_mdio_decode_mms_write_request(
        frame.data, frame.len, 0x10u, &out_addr, &out_words_data, &out_word_count, &txn));
    rcp_bytes_free(&frame);
}

//cfusa:test REQ-MDIO-022
//cfusa:test REQ-MDIO-024
static void test_mms_write_response_nothing_accepted(void)
{
    rcp_bytes_t    f = rcp_ep_mdio_encode_mms_write_response(0x10u, 0, NULL, 0, 5u, false, 0);
    const uint8_t  *out_words_data;
    size_t          out_word_count;
    bool            timed;
    uint64_t        ts;
    uint8_t         txn;

    TEST_ASSERT_NOT_NULL(f.data);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK, rcp_ep_mdio_decode_mms_write_response(
        f.data, f.len, 0x10u, 0, &out_words_data, &out_word_count, &timed, &ts, &txn));
    TEST_ASSERT_EQUAL_size_t(0u, out_word_count);

    rcp_bytes_free(&f);
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

    RUN_TEST(test_mode_for_word_count_single);
    RUN_TEST(test_mode_for_word_count_multi);
    RUN_TEST(test_mode_is_unsupported_mms);
    RUN_TEST(test_read_request_encode_sets_mdio_mode_single);
    RUN_TEST(test_read_request_encode_sets_mdio_mode_multi);
    RUN_TEST(test_write_request_encode_sets_mdio_mode_single);
    RUN_TEST(test_write_request_encode_sets_mdio_mode_multi);
    RUN_TEST(test_read_request_decode_rejects_mms_mode);
    RUN_TEST(test_write_request_decode_rejects_mms_mode);

    RUN_TEST(test_mms_addr_valid_in_range);
    RUN_TEST(test_mms_addr_valid_rejects_above_max);
    RUN_TEST(test_mms_uses_32bit_words_mms0_and_mms1);
    RUN_TEST(test_mms_uses_32bit_words_false_for_other_mms);
    RUN_TEST(test_mms_burst_next_addr_increments);
    RUN_TEST(test_mms_burst_next_addr_wraps_at_16_bits);
    RUN_TEST(test_mms_mode_for_word_count_single);
    RUN_TEST(test_mms_mode_for_word_count_multi);

    RUN_TEST(test_word32_encode_decode_round_trip);
    RUN_TEST(test_word32_encode_is_big_endian);
    RUN_TEST(test_mms_pack_len_32bit_for_mms0);
    RUN_TEST(test_mms_pack_len_16bit_for_other_mms);
    RUN_TEST(test_mms_pack_words_round_trip_32bit);
    RUN_TEST(test_mms_pack_words_round_trip_16bit);
    RUN_TEST(test_mms_pack_words_zero_count_returns_zeroed);
    RUN_TEST(test_mms_word_count_of_32bit_rejects_non_multiple_of_four);
    RUN_TEST(test_mms_word_count_of_16bit_rejects_odd_length);

    RUN_TEST(test_mms_read_request_round_trip_single_word_32bit);
    RUN_TEST(test_mms_read_request_round_trip_burst_16bit);
    RUN_TEST(test_mms_read_request_encode_rejects_invalid_addr);
    RUN_TEST(test_mms_read_request_encode_rejects_zero_word_count);
    RUN_TEST(test_mms_read_request_encode_rejects_word_count_above_max);
    RUN_TEST(test_mms_read_request_decode_rejects_wrong_bus);
    RUN_TEST(test_mms_read_request_decode_rejects_bad_mms_addr);
    RUN_TEST(test_mms_read_request_decode_rejects_zero_word_count);
    RUN_TEST(test_mms_read_request_decode_rejects_mmd_mode);
    RUN_TEST(test_mms_read_response_round_trip_untimed_32bit);
    RUN_TEST(test_mms_read_response_round_trip_timed_16bit);
    RUN_TEST(test_mms_read_response_decode_rejects_wrong_bus);
    RUN_TEST(test_mms_read_response_decode_rejects_bad_word_count_for_32bit_mms);

    RUN_TEST(test_mms_write_request_round_trip_single_word_32bit);
    RUN_TEST(test_mms_write_request_round_trip_burst_16bit);
    RUN_TEST(test_mms_write_request_encode_rejects_invalid_addr);
    RUN_TEST(test_mms_write_request_encode_rejects_zero_word_count);
    RUN_TEST(test_mms_write_request_decode_rejects_wrong_op);
    RUN_TEST(test_mms_write_request_decode_rejects_zero_words);
    RUN_TEST(test_mms_write_request_decode_rejects_mmd_mode);
    RUN_TEST(test_mms_write_response_round_trip_untimed);
    RUN_TEST(test_mms_write_response_nothing_accepted);

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

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_apply_reconfig_writes_ep_status);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_reconfig_strerror_never_null);

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
