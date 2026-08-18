/* SPDX-License-Identifier: MPL-2.0 */
/* REQ-LINEP-* atomicity audit (c-RCP-18-tracker, issue #533): every
 * requirement tag below is placed directly above the specific test
 * function that proves it, per CONTRIBUTING.md's "Writing a requirement"
 * convention -- no file-header stacked block any longer. */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_lin.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── evt[2:0]: exact-match response comparison ────────────────────────────── */

//cfusa:test REQ-LINEP-025
static void test_response_matches_exact(void)
{
    uint8_t req[3] = {0x10, 0x20, 0x30};
    uint8_t rx_same[3] = {0x10, 0x20, 0x30};
    uint8_t rx_diff[3] = {0x10, 0x20, 0x31};

    TEST_ASSERT_TRUE(rcp_ep_lin_response_matches(req, sizeof(req), rx_same, sizeof(rx_same)));
    TEST_ASSERT_FALSE(rcp_ep_lin_response_matches(req, sizeof(req), rx_diff, sizeof(rx_diff)));
    TEST_ASSERT_TRUE(rcp_ep_lin_response_matches(NULL, 0, NULL, 0));
}

/* TC18 §13.5.1's own length rule (see acf.h's rcp_acf_compound_wait_match()):
 * a shorter received message never matches; a longer one is compared only
 * up to the outgoing request's own length. */
//cfusa:test REQ-LINEP-025
static void test_response_matches_length_rule(void)
{
    uint8_t req[3] = {0x10, 0x20, 0x30};
    uint8_t rx_shorter[2] = {0x10, 0x20};
    uint8_t rx_longer_matching_prefix[4] = {0x10, 0x20, 0x30, 0x99};

    TEST_ASSERT_FALSE(
        rcp_ep_lin_response_matches(req, sizeof(req), rx_shorter, sizeof(rx_shorter)));
    TEST_ASSERT_TRUE(rcp_ep_lin_response_matches(req, sizeof(req), rx_longer_matching_prefix,
                                                  sizeof(rx_longer_matching_prefix)));
}

/* ── Transmission-done trigger ─────────────────────────────────────────────── */

/* REQ-LINEP-006: RCP_EP_LIN_TRIGGER_NONE never fires, regardless of either
 * input -- independent of REQ-LINEP-030's own TX_DONE clause below (split
 * 2026-08-18, c-RCP-18-tracker, issue #533; this used to be one shared
 * test covering both switch arms). */
//cfusa:test REQ-LINEP-006
static void test_trigger_fires_none_case(void)
{
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, true, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, false, false));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, false, true));
}

/* REQ-LINEP-030: TC18 §13.7.10.1 requires BOTH conditions -- TX_DONE fires
 * only when tx_done_event AND trailing_time_expired are true. */
//cfusa:test REQ-LINEP-030
static void test_trigger_fires_tx_done_case(void)
{
    TEST_ASSERT_TRUE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, true, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, false, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, true, false));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, false, false));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

//cfusa:test REQ-LINEP-007
static void test_functional_cfg_init_zeroes(void)
{
    rcp_ep_lin_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.common.ep_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.common.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.common.ep_suppress_response);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.lin_clk_divider);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_TRIGGER_NONE, cfg.trigger);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.wire_clk_divider);
}

//cfusa:test REQ-LINEP-008
static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_lin_functional_cfg_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

//cfusa:test REQ-LINEP-009
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
    TEST_ASSERT_FALSE(rcp_ep_lin_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_lin_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_lin_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_stream));
    TEST_ASSERT_TRUE(rcp_ep_lin_functional_cfg_writable(RCP_LIFECYCLE_HW_CONFIGURED, via_discovery));
}

//cfusa:test REQ-LINEP-010
static void test_functional_cfg_writable_rcp_configured_requires_authorization(void)
{
    rcp_lifecycle_writer_ctx_t none = {0};
    rcp_lifecycle_writer_ctx_t via_ep0 = {0};
    rcp_lifecycle_writer_ctx_t via_stream = {0};

    via_ep0.via_root_client_ep0 = true;
    via_stream.via_owning_stream = true;

    TEST_ASSERT_FALSE(rcp_ep_lin_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_TRUE(rcp_ep_lin_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_ep0));
    TEST_ASSERT_TRUE(rcp_ep_lin_functional_cfg_writable(
        RCP_LIFECYCLE_RCP_CONFIGURED, via_stream));
}

//cfusa:test REQ-LINEP-011
static void test_set_clk_divider_rejects_unauthorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_lin_set_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.lin_clk_divider);
}

//cfusa:test REQ-LINEP-012
static void test_set_clk_divider_applies_when_authorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_lin_set_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(42, cfg.lin_clk_divider);
}

//cfusa:test REQ-LINEP-013
static void test_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_lin_set_trigger(
        &cfg, RCP_EP_LIN_TRIGGER_TX_DONE, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_TRIGGER_NONE, cfg.trigger);
}

//cfusa:test REQ-LINEP-014
static void test_set_trigger_applies_when_authorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};
    writer.via_owning_stream = true;

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_lin_set_trigger(
        &cfg, RCP_EP_LIN_TRIGGER_TX_DONE, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_TRIGGER_TX_DONE, cfg.trigger);
}

/* ── The EP_func register block ────────────────────────────────────────────── */

//cfusa:test REQ-LINEP-028
//cfusa:test REQ-LINEP-024
static void test_render_registers_matches_table_offsets(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     out[RCP_EP_LIN_EP_FUNC_LEN];

    rcp_ep_lin_functional_cfg_init(&cfg);
    cfg.common.ep_enable = true;
    cfg.ep_status         = 0x1234;
    cfg.wire_clk_divider  = 0x55;

    rcp_ep_lin_render_registers(&cfg, out);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_EP_FUNC_LEN, out[RCP_EP_LIN_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_LIN_REG_RESERVED_01]);
    TEST_ASSERT_TRUE((out[RCP_EP_LIN_REG_EP_ENABLE_CLR] & 0x01u) != 0u);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_LIN_REG_BASE_CLK]);
    TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_LIN_REG_BASE_CLK + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x12u, out[RCP_EP_LIN_REG_EP_STATUS]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, out[RCP_EP_LIN_REG_EP_STATUS + 1]);
    TEST_ASSERT_EQUAL_UINT8(0x55, out[RCP_EP_LIN_REG_CLK_DIVIDER]);

    TEST_ASSERT_EQUAL_UINT16(0x0009u, RCP_EP_LIN_EP_FUNC_LEN);
}

//cfusa:test REQ-LINEP-038
static void test_apply_reconfig_writes_clk_divider(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_lin_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_LIN_REG_CLK_DIVIDER;
    payload[2] = 0x42;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_RECONFIG_OK,
        rcp_ep_lin_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0x42, cfg.wire_clk_divider);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.lin_clk_divider); /* untouched -- see the
                                                           file header */
}

//cfusa:test REQ-LINEP-038
static void test_apply_reconfig_writes_multi_register_span(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     payload[2 + 3];

    rcp_ep_lin_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = (uint8_t)RCP_EP_LIN_REG_EP_STATUS;
    payload[2] = 0xAB; payload[3] = 0xCD; /* ep_status */
    payload[4] = 0x77;                    /* clk_divider */

    TEST_ASSERT_EQUAL(RCP_EP_LIN_RECONFIG_OK,
        rcp_ep_lin_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, cfg.ep_status);
    TEST_ASSERT_EQUAL_UINT8(0x77, cfg.wire_clk_divider);
}

//cfusa:test REQ-LINEP-038
static void test_apply_reconfig_ignores_read_only_registers(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     payload[2 + 4];

    rcp_ep_lin_functional_cfg_init(&cfg);

    /* Cover EP_LEN (0x00), the reserved octet (0x01), and both octets of
     * base_clk (0x04-0x05) -- all read-only. */
    payload[0] = 0x00;
    payload[1] = 0x00;
    payload[2] = 0xFF;
    payload[3] = 0xFF;
    payload[4] = 0xFF;
    payload[5] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_RECONFIG_OK,
        rcp_ep_lin_apply_reconfig(&cfg, payload, sizeof(payload)));

    {
        uint8_t out[RCP_EP_LIN_EP_FUNC_LEN];

        rcp_ep_lin_render_registers(&cfg, out);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_EP_FUNC_LEN, out[RCP_EP_LIN_REG_EP_LEN]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_LIN_REG_RESERVED_01]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_LIN_REG_BASE_CLK]);
        TEST_ASSERT_EQUAL_UINT8(0, out[RCP_EP_LIN_REG_BASE_CLK + 1]);
    }
}

//cfusa:test REQ-LINEP-029
static void test_apply_reconfig_rejects_write_past_ep_len(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     payload[3];

    rcp_ep_lin_functional_cfg_init(&cfg);

    payload[0] = 0x00;
    payload[1] = 0x09; /* == RCP_EP_LIN_EP_FUNC_LEN -- one past the last
                           valid offset */
    payload[2] = 0xFF;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_RECONFIG_ERR_OUT_OF_RANGE,
        rcp_ep_lin_apply_reconfig(&cfg, payload, sizeof(payload)));
    TEST_ASSERT_EQUAL_UINT8(0, cfg.wire_clk_divider);
}

//cfusa:test REQ-LINEP-037
static void test_apply_reconfig_rejects_payload_without_data(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    uint8_t                     addr_only[2] = {0x00, 0x06};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_EQUAL(RCP_EP_LIN_RECONFIG_ERR_SHORT,
        rcp_ep_lin_apply_reconfig(&cfg, addr_only, sizeof(addr_only)));
    TEST_ASSERT_EQUAL(RCP_EP_LIN_RECONFIG_ERR_SHORT,
        rcp_ep_lin_apply_reconfig(&cfg, NULL, 0));
}

//cfusa:test REQ-LINEP-036
static void test_reconfig_request_round_trip(void)
{
    rcp_bytes_t                 frame;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    uint8_t                      data[2] = {0xAB, 0xCD};

    frame = rcp_ep_lin_encode_reconfig_request(0x03, 0x0006, data, sizeof(data), 7);
    TEST_ASSERT_NOT_NULL(frame.data);

    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8(0x03, hdr.byte_bus_id);
    TEST_ASSERT_EQUAL(RCP_ACF_OP_WRITE, hdr.op);
    TEST_ASSERT_EQUAL_UINT8(0x7u, hdr.evt);
    TEST_ASSERT_EQUAL_UINT8(7, hdr.transaction_num);
    TEST_ASSERT_EQUAL_UINT32(4, payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x00, payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0x06, payload[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, payload[2]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, payload[3]);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-036
static void test_encode_reconfig_request_rejects_empty_data(void)
{
    rcp_bytes_t frame = rcp_ep_lin_encode_reconfig_request(0x00, 0, NULL, 0, 0);

    TEST_ASSERT_NULL(frame.data);
}

//cfusa:test REQ-LINEP-039
static void test_reconfig_strerror_never_null(void)
{
    rcp_ep_lin_reconfig_errc_t codes[] = {
        RCP_EP_LIN_RECONFIG_OK, RCP_EP_LIN_RECONFIG_ERR_SHORT,
        RCP_EP_LIN_RECONFIG_ERR_OUT_OF_RANGE,
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_NOT_NULL(rcp_ep_lin_reconfig_strerror(codes[i]));
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_lin_reconfig_strerror((rcp_ep_lin_reconfig_errc_t)99));
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

//cfusa:test REQ-LINEP-015
static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_lin_errc_t codes[] = {
        RCP_EP_LIN_OK, RCP_EP_LIN_ERR_SHORT_FRAME, RCP_EP_LIN_ERR_BAD_MSG_TYPE,
        RCP_EP_LIN_ERR_WRONG_BUS, RCP_EP_LIN_ERR_WRONG_OP, RCP_EP_LIN_ERR_BAD_EVT,
    };
    size_t i, j;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const char *msg = rcp_ep_lin_strerror(codes[i]);

        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE(strlen(msg) > 0);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(msg, rcp_ep_lin_strerror(codes[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(rcp_ep_lin_strerror((rcp_ep_lin_errc_t)999));
}

/* ── Command request round trip ────────────────────────────────────────────── */

/* TC18 v0.5.1_RC §12.9.1 "Handling of requests" states the op field's
 * two senses:
 *
 *   A response with pay load data read from the EP is given, if requested
 *   by op=0 (read request) after the request has been executed.
 *   A response with err=0 and no payload is given after successful
 *   execution of a request with op=1 (write request) ...
 *
 * and §13.7.10.1 "LIN EP basic concept" states the LIN endpoint's own
 * reply rule in those same terms:
 *
 *   With pending read-requests the LIN endpoint checks each received
 *   message against the byte_msg_payload and if a match under the
 *   conditions given by evt[2:0] is found a reply is sent if op = 0.
 *
 * A LIN command request pushes bytes onto the bus AND expects the
 * received bytes back, so it is the op=0 (read) direction. Verified here
 * against the literal wire bit rather than against re-encoded output:
 * acf.h maps RCP_ACF_OP_READ onto wire op=0. This module previously
 * encoded op=1 and rejected op=0 -- exactly inverted.
 *
 * REQ-LINEP-026 RETIRED (c-RCP-18-tracker, REQ-LINEP-* atomicity audit,
 * issue #533): duplicated REQ-LINEP-016's own "evt = 0" encoding fact
 * under a separate id -- see .fusa-reqs.json for the full retirement
 * text. This vestigial tag keeps the retired id traceable to the test
 * that used to prove it. */
//cfusa:test REQ-LINEP-026
//cfusa:test REQ-LINEP-016
static void test_command_request_uses_read_direction_op(void)
{
    uint8_t                     tx[1] = {0x55};
    rcp_bytes_t                 frame = rcp_ep_lin_encode_command_request(6, tx, sizeof(tx), 3);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ACF_OP_READ, hdr.op);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-016
//cfusa:test REQ-LINEP-017
static void test_command_request_round_trip_carries_raw_bytes(void)
{
    /* Bytes model a client-constructed LIN frame (identifier/PID plus data
     * plus checksum) -- this module never parses or strips any of it, see
     * the file header. */
    uint8_t     tx[4] = {0x50, 0x10, 0x20, 0x7F};
    rcp_bytes_t frame = rcp_ep_lin_encode_command_request(6, tx, sizeof(tx), 7);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 6, &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-017
static void test_command_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame = rcp_ep_lin_encode_command_request(1, NULL, 0, 1);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 1, &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-032
static void test_command_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame = rcp_ep_lin_encode_command_request(4, tx, sizeof(tx), 0);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_WRONG_BUS,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 5, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* TC18 §13.5 Table 30: evt[2:0] must be 000b for a plain LIN command
 * request; every other value (except 111b's out-of-scope config-write
 * shape) is reserved and shall be rejected with UNSUPPORTED_CMD. */
//cfusa:test REQ-LINEP-027
static void test_command_request_rejects_bad_evt(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_READ;
    hdr.evt         = 0x3u; /* reserved, mid-range */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_BAD_EVT,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

/* The mirror of test_command_request_uses_read_direction_op(): a frame
 * carrying the write direction (§12.9.1's op=1, "no payload data
 * response") is not a LIN command request. */
//cfusa:test REQ-LINEP-033
static void test_command_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE; /* not a command request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_WRONG_OP,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-031
static void test_command_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_BAD_MSG_TYPE,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-018
static void test_command_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_SHORT_FRAME,
        rcp_ep_lin_decode_command_request(too_short, sizeof(too_short), 4, &out_tx, &out_tx_len,
                                           &txn));
}

/* ── Response round trip ───────────────────────────────────────────────────── */

/* REQ-LINEP-019/034 (split 2026-08-18, c-RCP-18-tracker, issue #533): the
 * round-trip tests below already exercise the untimed/timed encode shapes
 * together with rcp_ep_lin_decode_response()'s own dispatch, but do not by
 * themselves isolate encode_response()'s own message-type choice from
 * decode_response()'s own type-dispatch correctness -- a bug in one could
 * mask a matching bug in the other. These two tests check the wire
 * message type directly, independent of decode_response(). */
//cfusa:test REQ-LINEP-019
static void test_encode_response_untimed_uses_abb_message_type(void)
{
    uint8_t     rx[2] = {0xAA, 0xBB};
    rcp_bytes_t frame = rcp_ep_lin_encode_response(3, rx, sizeof(rx), 5, false, 0);
    uint8_t     msg_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(frame.data, frame.len, &msg_type));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MSG_TYPE_ABB, msg_type);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-034
static void test_encode_response_timed_uses_gbb_message_type(void)
{
    uint8_t     rx[2] = {0xAA, 0xBB};
    rcp_bytes_t frame = rcp_ep_lin_encode_response(3, rx, sizeof(rx), 5, true, 0xABCDEF0102030405ull);
    uint8_t     msg_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(frame.data, frame.len, &msg_type));
    TEST_ASSERT_EQUAL_UINT8(RCP_ACF_MSG_TYPE_GBB, msg_type);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-019
//cfusa:test REQ-LINEP-020
static void test_response_round_trip_untimed(void)
{
    uint8_t     rx[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    rcp_bytes_t frame = rcp_ep_lin_encode_response(2, rx, sizeof(rx), 11, false, 0);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = true;
    uint64_t    ts = 1;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed, &ts,
                                    &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_FALSE(timed);
    TEST_ASSERT_EQUAL_UINT64(0, ts);
    TEST_ASSERT_EQUAL_UINT8(11, txn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-021
static void test_response_round_trip_timed(void)
{
    uint8_t     rx[2] = {0x11, 0x22};
    rcp_bytes_t frame = rcp_ep_lin_encode_response(2, rx, sizeof(rx), 200, true,
                                                    0x0102030405060708ull);
    const uint8_t *out_rx = NULL;
    size_t      out_rx_len = 0;
    bool        timed = false;
    uint64_t    ts = 0;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_response(frame.data, frame.len, 2, &out_rx, &out_rx_len, &timed, &ts,
                                    &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(rx), out_rx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out_rx, sizeof(rx));
    TEST_ASSERT_TRUE(timed);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, ts);
    TEST_ASSERT_EQUAL_UINT8(200, txn);

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-035
static void test_response_decode_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_lin_encode_response(2, NULL, 0, 0, false, 0);
    const uint8_t *out_rx;
    size_t      out_rx_len;
    bool        timed;
    uint64_t    ts;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_WRONG_BUS,
        rcp_ep_lin_decode_response(frame.data, frame.len, 3, &out_rx, &out_rx_len, &timed, &ts,
                                    &txn));

    rcp_bytes_free(&frame);
}

//cfusa:test REQ-LINEP-022
static void test_response_decode_rejects_short_frame(void)
{
    uint8_t  too_short[2] = {RCP_ACF_MSG_TYPE_ABB, 0};
    const uint8_t *out_rx;
    size_t   out_rx_len;
    bool     timed;
    uint64_t ts;
    uint8_t  txn;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_SHORT_FRAME,
        rcp_ep_lin_decode_response(too_short, sizeof(too_short), 2, &out_rx, &out_rx_len, &timed,
                                    &ts, &txn));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_response_matches_exact);
    RUN_TEST(test_response_matches_length_rule);

    RUN_TEST(test_trigger_fires_none_case);
    RUN_TEST(test_trigger_fires_tx_done_case);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_hw_configured_requires_authorization_or_discovery_stream);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_clk_divider_rejects_unauthorized);
    RUN_TEST(test_set_clk_divider_applies_when_authorized);
    RUN_TEST(test_set_trigger_rejects_unauthorized);
    RUN_TEST(test_set_trigger_applies_when_authorized);

    RUN_TEST(test_render_registers_matches_table_offsets);
    RUN_TEST(test_apply_reconfig_writes_clk_divider);
    RUN_TEST(test_apply_reconfig_writes_multi_register_span);
    RUN_TEST(test_apply_reconfig_ignores_read_only_registers);
    RUN_TEST(test_apply_reconfig_rejects_write_past_ep_len);
    RUN_TEST(test_apply_reconfig_rejects_payload_without_data);
    RUN_TEST(test_reconfig_request_round_trip);
    RUN_TEST(test_encode_reconfig_request_rejects_empty_data);
    RUN_TEST(test_reconfig_strerror_never_null);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_command_request_uses_read_direction_op);
    RUN_TEST(test_command_request_round_trip_carries_raw_bytes);
    RUN_TEST(test_command_request_round_trip_empty_payload);
    RUN_TEST(test_command_request_rejects_wrong_bus);
    RUN_TEST(test_command_request_rejects_bad_evt);
    RUN_TEST(test_command_request_rejects_wrong_op);
    RUN_TEST(test_command_request_rejects_bad_msg_type);
    RUN_TEST(test_command_request_rejects_short_frame);

    RUN_TEST(test_encode_response_untimed_uses_abb_message_type);
    RUN_TEST(test_encode_response_timed_uses_gbb_message_type);
    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
