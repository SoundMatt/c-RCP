/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-LINEP-001
//cfusa:test REQ-LINEP-002
//cfusa:test REQ-LINEP-003
//cfusa:test REQ-LINEP-004
//cfusa:test REQ-LINEP-005
//cfusa:test REQ-LINEP-006
//cfusa:test REQ-LINEP-007
//cfusa:test REQ-LINEP-008
//cfusa:test REQ-LINEP-009
//cfusa:test REQ-LINEP-010
//cfusa:test REQ-LINEP-011
//cfusa:test REQ-LINEP-012
//cfusa:test REQ-LINEP-013
//cfusa:test REQ-LINEP-014
//cfusa:test REQ-LINEP-015
//cfusa:test REQ-LINEP-016
//cfusa:test REQ-LINEP-017
//cfusa:test REQ-LINEP-018
//cfusa:test REQ-LINEP-019
//cfusa:test REQ-LINEP-020
//cfusa:test REQ-LINEP-021
//cfusa:test REQ-LINEP-022
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/ep_lin.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── compare_mode_valid ────────────────────────────────────────────────────── */

static void test_compare_mode_valid_bounds(void)
{
    uint8_t v;

    for (v = 0; v <= 7; v++) {
        TEST_ASSERT_TRUE(rcp_ep_lin_compare_mode_valid(v));
    }
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_mode_valid(8));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_mode_valid(255));
}

/* ── compare_fires ──────────────────────────────────────────────────────────── */

static void test_compare_fires_exact(void)
{
    uint8_t req[3] = {0x10, 0x20, 0x30};
    uint8_t rx_same[3] = {0x10, 0x20, 0x30};
    uint8_t rx_diff[3] = {0x10, 0x20, 0x31};
    uint8_t rx_longer[4] = {0x10, 0x20, 0x30, 0x40};

    TEST_ASSERT_TRUE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_EXACT, req, sizeof(req), rx_same,
                                               sizeof(rx_same)));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_EXACT, req, sizeof(req), rx_diff,
                                                sizeof(rx_diff)));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_EXACT, req, sizeof(req),
                                                rx_longer, sizeof(rx_longer)));
    TEST_ASSERT_TRUE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_EXACT, NULL, 0, NULL, 0));
}

static void test_compare_fires_prefix(void)
{
    uint8_t req[2] = {0xAA, 0xBB};
    uint8_t rx_exact[2] = {0xAA, 0xBB};
    uint8_t rx_longer[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t rx_shorter[1] = {0xAA};
    uint8_t rx_mismatch[2] = {0xAA, 0x00};

    TEST_ASSERT_TRUE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_PREFIX, req, sizeof(req),
                                               rx_exact, sizeof(rx_exact)));
    TEST_ASSERT_TRUE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_PREFIX, req, sizeof(req),
                                               rx_longer, sizeof(rx_longer)));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_PREFIX, req, sizeof(req),
                                                rx_shorter, sizeof(rx_shorter)));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_PREFIX, req, sizeof(req),
                                                rx_mismatch, sizeof(rx_mismatch)));
}

static void test_compare_fires_any(void)
{
    uint8_t req[1] = {0x01};
    uint8_t rx[3] = {0x99, 0x98, 0x97};

    TEST_ASSERT_TRUE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_ANY, req, sizeof(req), rx,
                                               sizeof(rx)));
    TEST_ASSERT_TRUE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_ANY, NULL, 0, NULL, 0));
}

static void test_compare_fires_never_and_reserved_fail_safe(void)
{
    uint8_t req[1] = {0x01};
    uint8_t rx[1] = {0x01}; /* would satisfy EXACT/PREFIX/ANY, but must not
                                fire for NEVER or a reserved mode */

    TEST_ASSERT_FALSE(
        rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_NEVER, req, sizeof(req), rx, sizeof(rx)));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_RESERVED4, req, sizeof(req), rx,
                                                sizeof(rx)));
    TEST_ASSERT_FALSE(rcp_ep_lin_compare_fires(RCP_EP_LIN_COMPARE_RESERVED7, req, sizeof(req), rx,
                                                sizeof(rx)));
}

/* ── Transmission-done trigger ─────────────────────────────────────────────── */

static void test_trigger_fires(void)
{
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_NONE, false));
    TEST_ASSERT_TRUE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, true));
    TEST_ASSERT_FALSE(rcp_ep_lin_trigger_fires(RCP_EP_LIN_TRIGGER_TX_DONE, false));
}

/* ── Functional config ─────────────────────────────────────────────────────── */

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
}

static void test_functional_cfg_writable_false_hw_unconfigured(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    writer.via_root_client_ep0 = true;
    writer.via_owning_stream   = true;

    TEST_ASSERT_FALSE(rcp_ep_lin_functional_cfg_writable(
        RCP_LIFECYCLE_HW_UNCONFIGURED, writer));
}

static void test_functional_cfg_writable_true_hw_configured_any_writer(void)
{
    rcp_lifecycle_writer_ctx_t writer = {0};

    TEST_ASSERT_TRUE(rcp_ep_lin_functional_cfg_writable(
        RCP_LIFECYCLE_HW_CONFIGURED, writer));
}

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

static void test_set_clk_divider_rejects_unauthorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_lin_set_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT32(0, cfg.lin_clk_divider);
}

static void test_set_clk_divider_applies_when_authorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_lin_set_clk_divider(
        &cfg, 42, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT32(42, cfg.lin_clk_divider);
}

static void test_set_trigger_rejects_unauthorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     none = {0};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(rcp_ep_lin_set_trigger(
        &cfg, RCP_EP_LIN_TRIGGER_TX_DONE, RCP_LIFECYCLE_RCP_CONFIGURED, none));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_TRIGGER_NONE, cfg.trigger);
}

static void test_set_trigger_applies_when_authorized(void)
{
    rcp_ep_lin_functional_cfg_t cfg;
    rcp_lifecycle_writer_ctx_t     writer = {0};

    rcp_ep_lin_functional_cfg_init(&cfg);

    TEST_ASSERT_TRUE(rcp_ep_lin_set_trigger(
        &cfg, RCP_EP_LIN_TRIGGER_TX_DONE, RCP_LIFECYCLE_HW_CONFIGURED, writer));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_TRIGGER_TX_DONE, cfg.trigger);
}

/* ── strerror ───────────────────────────────────────────────────────────────── */

static void test_strerror_never_null_and_distinct(void)
{
    rcp_ep_lin_errc_t codes[] = {
        RCP_EP_LIN_OK, RCP_EP_LIN_ERR_SHORT_FRAME, RCP_EP_LIN_ERR_BAD_MSG_TYPE,
        RCP_EP_LIN_ERR_WRONG_BUS, RCP_EP_LIN_ERR_WRONG_OP,
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
 * encoded op=1 and rejected op=0 -- exactly inverted. */
static void test_command_request_uses_read_direction_op(void)
{
    uint8_t                     tx[1] = {0x55};
    rcp_bytes_t                 frame = rcp_ep_lin_encode_command_request(6, tx, sizeof(tx),
                                                                          RCP_EP_LIN_COMPARE_EXACT, 3);
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *payload;
    size_t                      payload_len;

    TEST_ASSERT_EQUAL_INT(RCP_ACF_OK,
                          rcp_acf_decode_abb(frame.data, frame.len, &hdr, &payload, &payload_len));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_ACF_OP_READ, hdr.op);

    rcp_bytes_free(&frame);
}

static void test_command_request_round_trip_carries_raw_bytes(void)
{
    /* Bytes model a client-constructed LIN frame (identifier/PID plus data
     * plus checksum) -- this module never parses or strips any of it, see
     * the file header. */
    uint8_t     tx[4] = {0x50, 0x10, 0x20, 0x7F};
    rcp_bytes_t frame = rcp_ep_lin_encode_command_request(6, tx, sizeof(tx),
                                                            RCP_EP_LIN_COMPARE_PREFIX, 7);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 0;
    uint8_t     mode = 0xFF;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 6, &out_tx, &out_tx_len, &mode,
                                           &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_COMPARE_PREFIX, mode);
    TEST_ASSERT_EQUAL_UINT8(7, txn);

    rcp_bytes_free(&frame);
}

static void test_command_request_round_trip_empty_payload(void)
{
    rcp_bytes_t frame =
        rcp_ep_lin_encode_command_request(1, NULL, 0, RCP_EP_LIN_COMPARE_NEVER, 1);
    const uint8_t *out_tx = NULL;
    size_t      out_tx_len = 1;
    uint8_t     mode = 0xFF;
    uint8_t     txn = 0;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 1, &out_tx, &out_tx_len, &mode,
                                           &txn));
    TEST_ASSERT_EQUAL_UINT32(0, out_tx_len);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_EP_LIN_COMPARE_NEVER, mode);

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_wrong_bus(void)
{
    uint8_t     tx[1] = {0xAB};
    rcp_bytes_t frame =
        rcp_ep_lin_encode_command_request(4, tx, sizeof(tx), RCP_EP_LIN_COMPARE_EXACT, 0);
    const uint8_t *out_tx;
    size_t      out_tx_len;
    uint8_t     mode;
    uint8_t     txn;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_WRONG_BUS,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 5, &out_tx, &out_tx_len, &mode,
                                           &txn));

    rcp_bytes_free(&frame);
}

/* The mirror of test_command_request_uses_read_direction_op(): a frame
 * carrying the write direction (§12.9.1's op=1, "no payload data
 * response") is not a LIN command request. */
static void test_command_request_rejects_wrong_op(void)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_bytes_t                 frame;
    const uint8_t               *out_tx;
    size_t                       out_tx_len;
    uint8_t                      mode;
    uint8_t                      txn;

    hdr.byte_bus_id = 4;
    hdr.op          = RCP_ACF_OP_WRITE; /* not a command request */
    frame = rcp_acf_encode_abb(&hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_WRONG_OP,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &mode,
                                           &txn));

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_bad_msg_type(void)
{
    rcp_acf_gbb_header_t gbb_hdr = {0};
    rcp_bytes_t          frame;
    const uint8_t        *out_tx;
    size_t                out_tx_len;
    uint8_t               mode;
    uint8_t               txn;

    gbb_hdr.info.byte_bus_id = 4;
    gbb_hdr.info.op          = RCP_ACF_OP_WRITE;
    frame = rcp_acf_encode_gbb(&gbb_hdr, NULL, 0);

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_BAD_MSG_TYPE,
        rcp_ep_lin_decode_command_request(frame.data, frame.len, 4, &out_tx, &out_tx_len, &mode,
                                           &txn));

    rcp_bytes_free(&frame);
}

static void test_command_request_rejects_short_frame(void)
{
    uint8_t        too_short[3] = {0};
    const uint8_t  *out_tx;
    size_t          out_tx_len;
    uint8_t         mode;
    uint8_t         txn;

    TEST_ASSERT_EQUAL(RCP_EP_LIN_ERR_SHORT_FRAME,
        rcp_ep_lin_decode_command_request(too_short, sizeof(too_short), 4, &out_tx, &out_tx_len,
                                           &mode, &txn));
}

/* ── Response round trip ───────────────────────────────────────────────────── */

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

    RUN_TEST(test_compare_mode_valid_bounds);

    RUN_TEST(test_compare_fires_exact);
    RUN_TEST(test_compare_fires_prefix);
    RUN_TEST(test_compare_fires_any);
    RUN_TEST(test_compare_fires_never_and_reserved_fail_safe);

    RUN_TEST(test_trigger_fires);

    RUN_TEST(test_functional_cfg_init_zeroes);
    RUN_TEST(test_functional_cfg_writable_false_hw_unconfigured);
    RUN_TEST(test_functional_cfg_writable_true_hw_configured_any_writer);
    RUN_TEST(test_functional_cfg_writable_rcp_configured_requires_authorization);

    RUN_TEST(test_set_clk_divider_rejects_unauthorized);
    RUN_TEST(test_set_clk_divider_applies_when_authorized);
    RUN_TEST(test_set_trigger_rejects_unauthorized);
    RUN_TEST(test_set_trigger_applies_when_authorized);

    RUN_TEST(test_strerror_never_null_and_distinct);

    RUN_TEST(test_command_request_uses_read_direction_op);
    RUN_TEST(test_command_request_round_trip_carries_raw_bytes);
    RUN_TEST(test_command_request_round_trip_empty_payload);
    RUN_TEST(test_command_request_rejects_wrong_bus);
    RUN_TEST(test_command_request_rejects_wrong_op);
    RUN_TEST(test_command_request_rejects_bad_msg_type);
    RUN_TEST(test_command_request_rejects_short_frame);

    RUN_TEST(test_response_round_trip_untimed);
    RUN_TEST(test_response_round_trip_timed);
    RUN_TEST(test_response_decode_rejects_wrong_bus);
    RUN_TEST(test_response_decode_rejects_short_frame);

    return UNITY_END();
}
